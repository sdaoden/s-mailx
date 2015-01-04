#!/bin/sh -
#@ Please see `INSTALL' and `make.rc' instead.

LC_ALL=C
export LC_ALL

# Predefined CONFIG= urations take precedence over anything else
if [ -n "${CONFIG}" ]; then
   case ${CONFIG} in
   MINIMAL)
      WANT_ICONV=1
      WANT_SOCKETS=0
      WANT_IDNA=0
      WANT_IMAP_SEARCH=0
      WANT_REGEX=0
      WANT_READLINE=0 WANT_EDITLINE=0 WANT_NCL=0
      WANT_SPAM=0
      WANT_DOCSTRINGS=0
      WANT_QUOTE_FOLD=0
      WANT_COLOUR=0
      ;;
   MEDIUM)
      WANT_ICONV=1
      WANT_SOCKETS=0
      WANT_IDNA=0
      WANT_IMAP_SEARCH=0
      WANT_REGEX=1
      WANT_READLINE=0 WANT_EDITLINE=0 WANT_NCL=1
         WANT_HISTORY=1 WANT_TABEXPAND=0
      WANT_SPAM=0
      WANT_DOCSTRINGS=1
      WANT_QUOTE_FOLD=0
      WANT_COLOUR=0
      ;;
   NETSEND)
      WANT_ICONV=1
      WANT_SOCKETS=1
         WANT_IPV6=1 WANT_SSL=1 WANT_SMTP=require WANT_POP3=0 WANT_IMAP=0
         WANT_GSSAPI=1 WANT_NETRC=1 WANT_AGENT=1
      WANT_IDNA=1
      WANT_IMAP_SEARCH=0
      WANT_REGEX=1
      WANT_READLINE=0 WANT_EDITLINE=0 WANT_NCL=1
         WANT_HISTORY=1 WANT_TABEXPAND=0
      WANT_SPAM=0
      WANT_DOCSTRINGS=1
      WANT_QUOTE_FOLD=0
      WANT_COLOUR=0
      ;;
   MAXIMAL)
      WANT_ICONV=1
      WANT_SOCKETS=1
         WANT_IPV6=1 WANT_SSL=1 WANT_SMTP=1 WANT_POP3=1 WANT_IMAP=1
         WANT_GSSAPI=1 WANT_NETRC=1 WANT_AGENT=1
      WANT_IDNA=1
      WANT_IMAP_SEARCH=1
      WANT_REGEX=1
      WANT_READLINE=0 WANT_EDITLINE=0 WANT_NCL=1
         WANT_HISTORY=1 WANT_TABEXPAND=1
      WANT_SPAM=1
      WANT_DOCSTRINGS=1
      WANT_QUOTE_FOLD=1
      WANT_COLOUR=1
      ;;
   *)
      echo >&2 "Unknown CONFIG= setting: ${CONFIG}"
      echo >&2 'Possible values: MINIMAL, MEDIUM, NETSEND, MAXIMAL'
      exit 1
   esac
fi

# Inter-relationships
option_update() {
   if feat_no SOCKETS; then
      if feat_require SMTP; then
         msg "ERROR: need SOCKETS for required feature SMTP\\n"
         config_exit 13
      fi
      if feat_require POP3; then
         msg "ERROR: need SOCKETS for required feature POP3\\n"
         config_exit 13
      fi
      if feat_require IMAP; then
         msg "ERROR: need SOCKETS for required feature IMAP\\n"
         config_exit 13
      fi
      WANT_IPV6=0 WANT_SSL=0
      WANT_SMTP=0 WANT_POP3=0 WANT_IMAP=0 WANT_GSSAPI=0
      WANT_NETRC=0 WANT_AGENT=0
   fi
   if feat_no SMTP && feat_no POP3 && feat_no IMAP; then
      WANT_SOCKETS=0 WANT_IPV6=0 WANT_SSL=0 WANT_NETRC=0 WANT_AGENT=0
   fi
   if feat_no SMTP && feat_no IMAP; then
      WANT_GSSAPI=0
   fi
   if feat_no READLINE && feat_no EDITLINE && feat_no NCL; then
      WANT_HISTORY=0 WANT_TABEXPAND=0
   fi
   # If we don't need MD5 except for producing boundary and message-id strings,
   # leave it off, plain old srand(3) should be enough for that purpose.
   if feat_no SOCKETS; then
      WANT_MD5=0
   fi
   if feat_yes DEBUG; then
      WANT_NOALLOCA=1 WANT_DEVEL=1
   fi
}

# Check out compiler ($CC) and -flags ($CFLAGS)
compiler_flags() {
   # $CC is overwritten when empty or a default "cc", even without WANT_AUTOCC
   optim= dbgoptim= _CFLAGS=
   if [ -z "${CC}" ] || [ "${CC}" = cc ]; then
      if { CC="`command -v clang`"; }; then
         :
      elif { CC="`command -v gcc`"; }; then
         :
      elif { CC="`command -v c89`"; }; then
         [ "`uname -s`" = UnixWare ] && _CFLAGS=-v optim=-O dbgoptim=
      elif { CC="`command -v c99`"; }; then
         :
      else
         msg 'ERROR'
         msg ' I cannot find a compiler!'
         msg ' Neither of clang(1), gcc(1), c89(1) and c99(1).'
         msg ' Please set the CC environment variable, maybe CFLAGS also.'
         config_exit 1
      fi
   fi
   export CC

   ccver=`${CC} --version 2>/dev/null`
   stackprot=no
   if { i=${ccver}; echo "${i}"; } | ${grep} -q -i -e gcc -e clang; then
   #if echo "${i}" | ${grep} -q -i -e gcc -e 'clang version 1'; then
      optim=-O2 dbgoptim=-O
      stackprot=yes
      _CFLAGS="${_CFLAGS} -std=c89 -Wall -Wextra -pedantic"
      _CFLAGS="${_CFLAGS} -fno-unwind-tables -fno-asynchronous-unwind-tables"
      _CFLAGS="${_CFLAGS} -fstrict-aliasing"
      _CFLAGS="${_CFLAGS} -Wbad-function-cast -Wcast-align -Wcast-qual"
      _CFLAGS="${_CFLAGS} -Winit-self -Wmissing-prototypes"
      _CFLAGS="${_CFLAGS} -Wshadow -Wunused -Wwrite-strings"
      _CFLAGS="${_CFLAGS} -Wno-long-long" # ISO C89 has no 'long long'...
      if { i=${ccver}; echo "${i}"; } | ${grep} -q -e 'clang version 1'; then
         _CFLAGS="${_CFLAGS} -Wstrict-overflow=5"
      else
         _CFLAGS="${_CFLAGS} -fstrict-overflow -Wstrict-overflow=5"
         if feat_yes AMALGAMATION && feat_no DEBUG; then
            _CFLAGS="${_CFLAGS} -Wno-unused-function"
         fi
         if { i=${ccver}; echo "${i}"; } | ${grep} -q -i -e clang; then
            _CFLAGS="${_CFLAGS} -Wno-unused-result" # TODO handle the right way
         fi
      fi
      if feat_yes AMALGAMATION; then
         _CFLAGS="${_CFLAGS} -pipe"
      fi
#   elif { i=${ccver}; echo "${i}"; } | ${grep} -q -i -e clang; then
#      optim=-O3 dbgoptim=-O
#      stackprot=yes
#      _CFLAGS='-std=c89 -Weverything -Wno-long-long'
#      if feat_yes AMALGAMATION; then
#         _CFLAGS="${_CFLAGS} -pipe"
#      fi
   elif [ -z "${optim}" ]; then
      optim=-O1 dbgoptim=-O
   fi

   if feat_no DEBUG; then
      _CFLAGS="${optim} -DNDEBUG ${_CFLAGS}"
   else
      _CFLAGS="${dbgoptim} -g ${_CFLAGS}";
      if [ "${stackprot}" = yes ]; then
         _CFLAGS="${_CFLAGS} -ftrapv -fstack-protector-all "
            _CFLAGS="${_CFLAGS} -Wstack-protector -D_FORTIFY_SOURCE=2"
      fi
   fi
   _CFLAGS="${_CFLAGS} ${ADDCFLAGS}"
   # XXX -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack: need detection
   _LDFLAGS="${_LDFLAGS} ${ADDLDFLAGS}" # XXX -Wl,--sort-common,[-O1]
   export _CFLAGS _LDFLAGS

   # $CFLAGS and $LDFLAGS are only overwritten if explicitly wanted
   if feat_yes AUTOCC; then
      CFLAGS=$_CFLAGS
      LDFLAGS=$_LDFLAGS
      export CFLAGS LDFLAGS
   fi
}

##  --  >8  --  8<  --  ##

## Notes:
## - Heirloom sh(1) (and same origin) have _sometimes_ problems with ': >'
##   redirection, so use "printf '' >" instead

## Very first: we undergo several states regarding I/O redirection etc.,
## but need to deal with option updates from within all.  Since all the
## option stuff should be above the scissor line, define utility functions
## and redefine them as necessary.
## And, since we have those functions, simply use them for whatever

config_exit() {
   exit ${1}
}

msg() {
   fmt=${1}
   shift
   printf >&2 "${fmt}\\n" "${@}"
}

## First of all, create new configuration and check wether it changed ##

rc=./make.rc
lst=./config.lst
h=./config.h
mk=./mk.mk

newlst=./config.lst-new
newmk=./config.mk-new
newh=./config.h-new
tmp0=___tmp
tmp=./${tmp0}1$$

# We need some standard utilities
unset -f command
check_tool() {
   n=${1} i=${2} opt=${3:-0}
   # Evaluate, just in case user comes in with shell snippets (..well..)
   eval i="${i}"
   if type "${i}" >/dev/null 2>&1; then
      eval ${n}=${i}
      return 0
   fi
   if [ ${opt} -eq 0 ]; then
      msg "ERROR: no trace of the utility \`${n}'\\n"
      config_exit 1
   fi
   return 0
}

# Check those tools right now that we need before including $rc
check_tool rm "${rm:-`command -v rm`}"
check_tool sed "${sed:-`command -v sed`}"

# Include $rc, but only take from it what wasn't overwritten by the user from
# within the command line or from a chosen fixed CONFIG=
# Note we leave alone the values
trap "${rm} -f ${tmp}; exit" 1 2 15
trap "${rm} -f ${tmp}" 0

${rm} -f ${tmp}
< ${rc} ${sed} -e '/^[ \t]*#/d' -e '/^$/d' -e 's/[ \t]*$//' |
while read line; do
   i="`echo ${line} | ${sed} -e 's/=.*$//'`"
   eval j="\$${i}" jx="\${${i}+x}"
   if [ -n "${j}" ] || [ "${jx}" = x ]; then
      : # Yet present
   else
      j="`echo ${line} | ${sed} -e 's/^[^=]*=//' -e 's/^\"*//' -e 's/\"*$//'`"
   fi
   echo "${i}=\"${j}\""
done > ${tmp}
# Reread the mixed version right now
. ./${tmp}

check_tool awk "${awk:-`command -v awk`}"
check_tool cat "${cat:-`command -v cat`}"
check_tool chmod "${chmod:-`command -v chmod`}"
check_tool cp "${cp:-`command -v cp`}"
check_tool cmp "${cmp:-`command -v cmp`}"
check_tool grep "${grep:-`command -v grep`}"
check_tool mkdir "${mkdir:-`command -v mkdir`}"
check_tool mv "${mv:-`command -v mv`}"
# rm(1), sed(1) above
check_tool tee "${tee:-`command -v tee`}"

check_tool make "${MAKE:-`command -v make`}"
check_tool strip "${STRIP:-`command -v strip`}" 1
HAVE_STRIP=${?}

# Update WANT_ options now, in order to get possible inter-dependencies right

feat_val_no() {
   [ "x${1}" = x0 ] || [ "x${1}" = xfalse ] || [ "x${1}" = xno ]
}

feat_val_yes() {
   [ "x${1}" = x1 ] || [ "x${1}" = xtrue ] || [ "x${1}" = xyes ] ||
         [ "x${1}" = xrequire ]
}

feat_val_require() {
   [ "x${1}" = xrequire ]
}

_feat_check() {
   eval i=\$WANT_${1}
   i="`echo ${i} | tr '[A-Z]' '[a-z]'`"
   if feat_val_no "${i}"; then
      return 1
   elif feat_val_yes "${i}"; then
      return 0
   else
      msg "ERROR: ${1}: allowed: 0/false/no or 1/true/yes/require, got: ${i}\\n"
      config_exit 11
   fi
}

feat_yes() {
   _feat_check ${1}
}

feat_no() {
   _feat_check ${1} && return 1
   return 0
}

feat_require() {
   eval i=\$WANT_${1}
   i="`echo ${i} | tr '[A-Z]' '[a-z]'`"
   [ "x${i}" = xrequire ]
}

option_update

# (No functions since some shells loose non-exported variables in traps)
trap "${rm} -f ${tmp} ${newlst} ${newmk} ${newh}; exit" 1 2 15
trap "${rm} -f ${tmp} ${newlst} ${newmk} ${newh}" 0

# Our configuration options may at this point still contain shell snippets,
# we need to evaluate them in order to get them expanded, and we need those
# evaluated values not only in our new configuration file, but also at hand..
${rm} -f ${newlst} ${newmk} ${newh}
exec 5<&0 6>&1 <${tmp} >${newlst}
while read line; do
   i=`echo ${line} | ${sed} -e 's/=.*$//'`
   eval j=\$${i}
   if echo "${i}" | grep -e '^WANT_' >/dev/null 2>&1; then
      if [ -z "${j}" ] || feat_val_no "${j}"; then
         j=0
         printf "/*#define ${i}*/\n" >> ${newh}
      elif feat_val_yes "${j}"; then
         if feat_val_require "${j}"; then
            j=require
         else
            j=1
         fi
         printf "#define ${i}\n" >> ${newh}
      else
         msg "ERROR: internal error -42\\n"
         config_exit 1
      fi
   else
      printf "#define ${i} \"${j}\"\n" >> ${newh}
   fi
   printf "${i} = ${j}\n" >> ${newmk}
   printf "${i}=${j}\n"
   eval "${i}=\"${j}\""
done
exec 0<&5 1<&6 5<&- 6<&-

printf "#define UAGENT \"${SID}${NAIL}\"\n" >> ${newh}
printf "UAGENT = ${SID}${NAIL}\n" >> ${newmk}

compiler_flags

printf "CC = ${CC}\n" >> ${newmk}
printf "CC=${CC}\n" >> ${newlst}

printf "_CFLAGS = ${_CFLAGS}\nCFLAGS = ${CFLAGS}\n" >> ${newmk}
printf "_CFLAGS=${_CFLAGS}\nCFLAGS=${CFLAGS}\n" >> ${newlst}

printf "_LDFLAGS = ${_LDFLAGS}\nLDFLAGS = ${LDFLAGS}\n" >> ${newmk}
printf "_LDFLAGS=${_LDFLAGS}\nLDFLAGS=${LDFLAGS}\n" >> ${newlst}

printf "AWK = ${awk}\nCMP = ${cmp}\nCHMOD = ${chmod}\nCP = ${cp}\n" >> ${newmk}
printf "AWK=${awk}\nCMP=${cmp}\nCHMOD=${chmod}\nCP=${cp}\n" >> ${newlst}

printf "GREP = ${grep}\nMKDIR = ${mkdir}\nRM = ${rm}\nSED = ${sed}\n" \
   >> ${newmk}
printf "GREP=${grep}\nMKDIR=${mkdir}\nRM=${rm}\nSED=${sed}\n" >> ${newlst}

printf "STRIP = ${strip}\nHAVE_STRIP = ${HAVE_STRIP}\n" >> ${newmk}
printf "STRIP=${strip}\nHAVE_STRIP=${HAVE_STRIP}\n" >> ${newlst}

# Build a basic set of INCS and LIBS according to user environment.
# On pkgsrc(7) systems automatically add /usr/pkg/*
if [ -n "${C_INCLUDE_PATH}" ]; then
   i=${IFS}
   IFS=:
   set -- ${C_INCLUDE_PATH}
   IFS=${i}
   # for i; do -- new in POSIX Issue 7 + TC1
   for i
   do
      [ "${i}" = /usr/pkg/include ] && continue
      INCS="${INCS} -I${i}"
   done
fi
[ -d /usr/pkg/include ] && INCS="${INCS} -I/usr/pkg/include"
printf "INCS=${INCS}\n" >> ${newlst}

if [ -n "${LD_LIBRARY_PATH}" ]; then
   i=${IFS}
   IFS=:
   set -- ${LD_LIBRARY_PATH}
   IFS=${i}
   # for i; do -- new in POSIX Issue 7 + TC1
   for i
   do
      [ "${i}" = /usr/pkg/lib ] && continue
      LIBS="${LIBS} -L${i}"
   done
fi
[ -d /usr/pkg/lib ] && LIBS="${LIBS} -L/usr/pkg/lib"
printf "LIBS=${LIBS}\n" >> ${newlst}

# Now finally check wether we already have a configuration and if so, wether
# all those parameters are still the same.. or something has actually changed
if [ -f ${lst} ] && ${cmp} ${newlst} ${lst} >/dev/null 2>&1; then
   exit 0
fi
[ -f ${lst} ] && echo 'configuration updated..' || echo 'shiny configuration..'

# Time to redefine helper 1
config_exit() {
   ${rm} -f ${lst} ${h} ${mk}
   exit ${1}
}

${mv} -f ${newlst} ${lst}
${mv} -f ${newh} ${h}
${mv} -f ${newmk} ${mk}

## Compile and link checking ##

tmp2=./${tmp0}2$$
tmp3=./${tmp0}3$$
log=./config.log
lib=./config.lib
inc=./config.inc
src=./config.c
makefile=./config.mk

# (No function since some shells loose non-exported variables in traps)
trap "${rm} -f ${lst} ${h} ${mk} ${lib} ${inc} ${src} ${makefile}; exit" 1 2 15
trap "${rm} -f ${tmp0}.* ${tmp0}* ${makefile}" 0

# Time to redefine helper 2
msg() {
   fmt=${1}
   shift
   printf "*** ${fmt}\\n" "${@}"
   printf "${fmt}" "${@}" >&5
}

exec 5>&2 > ${log} 2>&1

echo "${LIBS}" > ${lib}
echo "${INCS}" > ${inc}
# ${src} is only created if WANT_AMALGAMATION
${rm} -f ${src}
${cat} > ${makefile} << \!
.SUFFIXES: .o .c .x .y
.c.o:
	$(CC) $(XINCS) -c $<
.c.x:
	$(CC) $(XINCS) -E $< >$@
.c:
	$(CC) $(XINCS) -o $@ $< $(XLIBS)
.y: ;
!

_check_preface() {
   variable=$1 topic=$2 define=$3

   echo '**********'
   msg "checking ${topic} ... "
   echo "/* checked ${topic} */" >> ${h}
   ${rm} -f ${tmp} ${tmp}.o
   echo '*** test program is'
   ${tee} ${tmp}.c
   #echo '*** the preprocessor generates'
   #${make} -f ${makefile} ${tmp}.x
   #${cat} ${tmp}.x
   echo '*** results are'
}

compile_check() {
   variable=$1 topic=$2 define=$3

   _check_preface "${variable}" "${topic}" "${define}"

   if ${make} -f ${makefile} XINCS="${INCS}" ./${tmp}.o &&
         [ -f ./${tmp}.o ]; then
      msg "yes\\n"
      echo "${define}" >> ${h}
      eval have_${variable}=yes
      return 0
   else
      echo "/* ${define} */" >> ${h}
      msg "no\\n"
      eval unset have_${variable}
      return 1
   fi
}

_link_mayrun() {
   run=$1 variable=$2 topic=$3 define=$4 libs=$5 incs=$6

   _check_preface "${variable}" "${topic}" "${define}"

   if ${make} -f ${makefile} XINCS="${INCS} ${incs}" \
            XLIBS="${LIBS} ${libs}" ./${tmp} &&
         [ -f ./${tmp} ] &&
         { [ ${run} -eq 0 ] || ./${tmp}; }; then
      echo "*** adding INCS<${incs}> LIBS<${libs}>"
      msg "yes\\n"
      echo "${define}" >> ${h}
      LIBS="${LIBS} ${libs}"
      echo "${libs}" >> ${lib}
      INCS="${INCS} ${incs}"
      echo "${incs}" >> ${inc}
      eval have_${variable}=yes
      return 0
   else
      msg "no\\n"
      echo "/* ${define} */" >> ${h}
      eval unset have_${variable}
      return 1
   fi
}

link_check() {
   _link_mayrun 0 "${1}" "${2}" "${3}" "${4}" "${5}"
}

run_check() {
   _link_mayrun 1 "${1}" "${2}" "${3}" "${4}" "${5}"
}

feat_bail_required() {
   if feat_require ${1}; then
      msg "ERROR: feature WANT_${1} is required but not available\\n"
      config_exit 13
   fi
   eval WANT_${1}=0
   option_update # XXX this is rather useless here (dependency chain..)
}

##

# Better set _GNU_SOURCE (if we are on Linux only?); 'surprised it did without!
echo '#define _GNU_SOURCE' >> ${h}

if link_check hello 'if a hello world program can be built' << \!
#include <stdio.h>

int main(int argc, char *argv[])
{
   (void)argc;
   (void)argv;
   puts("hello world");
   return 0;
}
!
then
   :
else
   echo >&5 'This oooops is most certainly not related to me.'
   echo >&5 "Read the file ${log} and check your compiler environment."
   config_exit 1
fi

if link_check termios 'for termios.h and tc*() family' << \!
#include <termios.h>
int main(void)
{
   struct termios tios;
   tcgetattr(0, &tios);
   tcsetattr(0, TCSADRAIN | TCSAFLUSH, &tios);
   return 0;
}
!
then
   :
else
   echo >&5 'We require termios.h and the tc*() family of functions.'
   echo >&5 "That much Unix we indulge ourselfs."
   config_exit 1
fi

link_check setenv 'for setenv()/unsetenv()' '#define HAVE_SETENV' << \!
#include <stdlib.h>
int main(void)
{
   setenv("s-nail", "to be made nifty!", 1);
   unsetenv("s-nail");
   return 0;
}
!

link_check snprintf 'for snprintf()' '#define HAVE_SNPRINTF' << \!
#include <stdio.h>
int main(void)
{
   char b[20];
   snprintf(b, sizeof b, "%s", "string");
   return 0;
}
!

link_check putc_unlocked 'for putc_unlocked()' '#define HAVE_PUTC_UNLOCKED' <<\!
#include <stdio.h>
int main(void)
{
   putc_unlocked('@', stdout);
   return 0;
}
!

link_check fchdir 'for fchdir()' '#define HAVE_FCHDIR' << \!
#include <unistd.h>
int main(void)
{
   fchdir(0);
   return 0;
}
!

link_check pipe2 'for pipe2()' '#define HAVE_PIPE2' << \!
#include <fcntl.h>
#include <unistd.h>
int main(void)
{
   int fds[2];
   pipe2(fds, O_CLOEXEC);
   return 0;
}
!

link_check mmap 'for mmap()' '#define HAVE_MMAP' << \!
#include <sys/types.h>
#include <sys/mman.h>
int main(void)
{
   mmap(0, 0, 0, 0, 0, 0);
   return 0;
}
!

link_check mremap 'for mremap()' '#define HAVE_MREMAP' << \!
#include <sys/types.h>
#include <sys/mman.h>
int main(void)
{
   mremap(0, 0, 0, MREMAP_MAYMOVE);
   return 0;
}
!

link_check setlocale 'for setlocale()' '#define HAVE_SETLOCALE' << \!
#include <locale.h>
int main(void)
{
   setlocale(LC_ALL, "");
   return 0;
}
!

if [ "${have_setlocale}" = yes ]; then
   link_check c90amend1 'for ISO/IEC 9899:1990/Amendment 1:1995' \
      '#define HAVE_C90AMEND1' << \!
#include <limits.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
int main(void)
{
   char mbb[MB_LEN_MAX + 1];
   wchar_t wc;
   iswprint(L'c');
   towupper(L'c');
   mbtowc(&wc, "x", 1);
   mbrtowc(&wc, "x", 1, NULL);
   wctomb(mbb, wc);
   return (mblen("\0", 1) == 0);
}
!

   if [ "${have_c90amend1}" = yes ]; then
      link_check wcwidth 'for wcwidth()' '#define HAVE_WCWIDTH' << \!
#include <wchar.h>
int main(void)
{
   wcwidth(L'c');
   return 0;
}
!
   fi

   link_check nl_langinfo 'for nl_langinfo()' '#define HAVE_NL_LANGINFO' << \!
#include <langinfo.h>
#include <stdlib.h>
int main(void)
{
   nl_langinfo(DAY_1);
   return (nl_langinfo(CODESET) == NULL);
}
!
fi # have_setlocale

link_check mkstemp 'for mkstemp()' '#define HAVE_MKSTEMP' << \!
#include <stdlib.h>
int main(void)
{
   mkstemp("x");
   return 0;
}
!

# Note: run_check, thus we also get only the desired implementation...
run_check realpath 'for realpath()' '#define HAVE_REALPATH' << \!
#include <stdlib.h>
int main(void)
{
   char *x = realpath(".", NULL), *y = realpath("/", NULL);
   return (x != NULL && y != NULL) ? 0 : 1;
}
!

link_check wordexp 'for wordexp()' '#define HAVE_WORDEXP' << \!
#include <wordexp.h>
int main(void)
{
   wordexp((char*)0, (wordexp_t*)0, 0);
   return 0;
}
!

##

if feat_yes DEBUG; then
   echo '#define HAVE_DEBUG' >> ${h}
fi

if feat_yes AMALGAMATION; then
   echo '#define HAVE_AMALGAMATION' >> ${h}
fi

if feat_no NOALLOCA; then
   # Due to NetBSD PR lib/47120 it seems best not to use non-cc-builtin
   # versions of alloca(3) since modern compilers just can't be trusted
   # not to overoptimize and silently break some code
   link_check alloca 'for __builtin_alloca()' \
      '#define HAVE_ALLOCA __builtin_alloca' << \!
int main(void)
{
   void *vp = __builtin_alloca(1);
   return (vp != NULL);
}
!
fi

if feat_yes DEVEL; then
   echo '#define HAVE_DEVEL' >> ${h}
fi

if feat_yes NYD2; then
   echo '#define HAVE_NYD2' >> ${h}
fi

##

if feat_yes ICONV; then
   ${cat} > ${tmp2}.c << \!
#include <iconv.h>
int main(void)
{
   iconv_t id;

   id = iconv_open("foo", "bar");
   return 0;
}
!
   < ${tmp2}.c link_check iconv 'for iconv functionality' \
         '#define HAVE_ICONV' ||
      < ${tmp2}.c link_check iconv \
         'for iconv functionality in libiconv' \
         '#define HAVE_ICONV' '-liconv' ||
      feat_bail_required ICONV
else
   echo '/* WANT_ICONV=0 */' >> ${h}
fi # feat_yes ICONV

if feat_yes SOCKETS; then
   compile_check arpa_inet_h 'for <arpa/inet.h>' \
      '#define HAVE_ARPA_INET_H' << \!
#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
!

   ${cat} > ${tmp2}.c << \!
#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

int main(void)
{
   struct sockaddr s;
   socket(AF_INET, SOCK_STREAM, 0);
   connect(0, &s, 0);
   gethostbyname("foo");
   return 0;
}
!

   < ${tmp2}.c link_check sockets 'for sockets in libc' \
         '#define HAVE_SOCKETS' ||
      < ${tmp2}.c link_check sockets 'for sockets in libnsl' \
         '#define HAVE_SOCKETS' '-lnsl' ||
      < ${tmp2}.c link_check sockets \
         'for sockets in libsocket and libnsl' \
         '#define HAVE_SOCKETS' '-lsocket -lnsl' ||
      feat_bail_required SOCKETS
else
   echo '/* WANT_SOCKETS=0 */' >> ${h}
fi # feat_yes SOCKETS

feat_yes SOCKETS &&
link_check setsockopt 'for setsockopt()' '#define HAVE_SETSOCKOPT' << \!
#include <sys/socket.h>
#include <stdlib.h>
int main(void)
{
   int sockfd = 3;
   setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, NULL, 0);
   return 0;
}
!

feat_yes SOCKETS && [ -n "${have_setsockopt}" ] &&
link_check so_sndtimeo 'for SO_SNDTIMEO' '#define HAVE_SO_SNDTIMEO' << \!
#include <sys/socket.h>
#include <stdlib.h>
int main(void)
{
   struct timeval tv;
   int sockfd = 3;
   tv.tv_sec = 42;
   tv.tv_usec = 21;
   setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
   setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
   return 0;
}
!

feat_yes SOCKETS && [ -n "${have_setsockopt}" ] &&
link_check so_linger 'for SO_LINGER' '#define HAVE_SO_LINGER' << \!
#include <sys/socket.h>
#include <stdlib.h>
int main(void)
{
   struct linger li;
   int sockfd = 3;
   li.l_onoff = 1;
   li.l_linger = 42;
   setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &li, sizeof li);
   return 0;
}
!

if feat_yes IPV6; then
   if link_check ipv6 'for IPv6 functionality' '#define HAVE_IPV6' << \!
#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

int main(void)
{
   struct addrinfo a, *ap;
   getaddrinfo("foo", "0", &a, &ap);
   return 0;
}
!
   then
      :
   else
      feat_bail_required IPV6
   fi
else
   echo '/* WANT_IPV6=0 */' >> ${h}
fi # feat_yes IPV6

if feat_yes SSL; then
   if link_check openssl 'for sufficiently recent OpenSSL' \
      '#define HAVE_SSL
      #define HAVE_OPENSSL' '-lssl -lcrypto' << \!
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

#if defined OPENSSL_NO_SSL2 && defined OPENSSL_NO_SSL3 &&\
      defined OPENSSL_NO_TLS1
# error We need one of (SSLv2 and) SSLv3 and TLS1.
#endif

int main(void)
{
   SSLv23_client_method();
#ifndef OPENSSL_NO_SSL3
   SSLv3_client_method();
#endif
#ifndef OPENSSL_NO_TLS1
   TLSv1_client_method();
# ifdef TLS1_1_VERSION
   TLSv1_1_client_method();
# endif
# ifdef TLS1_2_VERSION
   TLSv1_2_client_method();
# endif
#endif
   PEM_read_PrivateKey(0, 0, 0, 0);
   return 0;
}
!
   then
      :
   else
      feat_bail_required SSL
   fi

   if [ "${have_openssl}" = 'yes' ]; then
      compile_check stack_of 'for OpenSSL STACK_OF()' \
         '#define HAVE_OPENSSL_STACK_OF' << \!
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

int main(void)
{
   STACK_OF(GENERAL_NAME) *gens = NULL;
   printf("%p", gens); /* to make it used */
   return 0;
}
!

      link_check rand_egd 'for OpenSSL RAND_egd()' \
         '#define HAVE_OPENSSL_RAND_EGD' '-lssl -lcrypto' << \!
#include <openssl/rand.h>

int main(void)
{
   return RAND_egd("some.where") > 0;
}
!

      if feat_yes MD5 && feat_no NOEXTMD5; then
         run_check openssl_md5 'for MD5 digest in OpenSSL' \
         '#define HAVE_OPENSSL_MD5' << \!
#include <string.h>
#include <openssl/md5.h>

int main(void)
{
   char const dat[] = "abrakadabrafidibus";
   char dig[16], hex[16 * 2];
   MD5_CTX ctx;
   size_t i, j;

   memset(dig, 0, sizeof(dig));
   memset(hex, 0, sizeof(hex));
   MD5_Init(&ctx);
   MD5_Update(&ctx, dat, sizeof(dat) - 1);
   MD5_Final(dig, &ctx);

#define hexchar(n) ((n) > 9 ? (n) - 10 + 'a' : (n) + '0')
   for (i = 0; i < sizeof(hex) / 2; i++) {
      j = i << 1;
      hex[j] = hexchar((dig[i] & 0xf0) >> 4);
      hex[++j] = hexchar(dig[i] & 0x0f);
   }
   return !!memcmp("6d7d0a3d949da2e96f2aa010f65d8326", hex, sizeof(hex));
}
!
      fi # feat_yes MD5 && feat_no NOEXTMD5
   fi
else
   echo '/* WANT_SSL=0 */' >> ${h}
fi # feat_yes SSL

if feat_yes SMTP; then
   echo '#define HAVE_SMTP' >> ${h}
else
   echo '/* WANT_SMTP=0 */' >> ${h}
fi

if feat_yes POP3; then
   echo '#define HAVE_POP3' >> ${h}
else
   echo '/* WANT_POP3=0 */' >> ${h}
fi

if feat_yes IMAP; then
   echo '#define HAVE_IMAP' >> ${h}
else
   echo '/* WANT_IMAP=0 */' >> ${h}
fi

if feat_yes GSSAPI; then
   ${cat} > ${tmp2}.c << \!
#include <gssapi/gssapi.h>

int main(void)
{
   gss_import_name(0, 0, GSS_C_NT_HOSTBASED_SERVICE, 0);
   gss_init_sec_context(0,0,0,0,0,0,0,0,0,0,0,0,0);
   return 0;
}
!
   ${sed} -e '1s/gssapi\///' < ${tmp2}.c > ${tmp3}.c

   if command -v krb5-config >/dev/null 2>&1; then
      i=`command -v krb5-config`
      GSS_LIBS="`CFLAGS= ${i} --libs gssapi`"
      GSS_INCS="`CFLAGS= ${i} --cflags`"
      i='for GSS-API via krb5-config(1)'
   else
      GSS_LIBS='-lgssapi'
      GSS_INCS=
      i='for GSS-API in gssapi/gssapi.h, libgssapi'
   fi
   if < ${tmp2}.c link_check gss \
         "${i}" '#define HAVE_GSSAPI' "${GSS_LIBS}" "${GSS_INCS}" ||\
      < ${tmp3}.c link_check gss \
         'for GSS-API in gssapi.h, libgssapi' \
         '#define HAVE_GSSAPI
         #define GSSAPI_REG_INCLUDE' \
         '-lgssapi' ||\
      < ${tmp2}.c link_check gss 'for GSS-API in libgssapi_krb5' \
         '#define HAVE_GSSAPI' \
         '-lgssapi_krb5' ||\
      < ${tmp3}.c link_check gss \
         'for GSS-API in libgssapi, OpenBSD-style (pre 5.3)' \
         '#define HAVE_GSSAPI
         #define GSS_REG_INCLUDE' \
         '-lgssapi -lkrb5 -lcrypto' \
         '-I/usr/include/kerberosV' ||\
      < ${tmp2}.c link_check gss 'for GSS-API in libgss' \
         '#define HAVE_GSSAPI' \
         '-lgss' ||\
      link_check gss 'for GSS-API in libgssapi_krb5, old-style' \
         '#define HAVE_GSSAPI
         #define GSSAPI_OLD_STYLE' \
         '-lgssapi_krb5' << \!
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_generic.h>

int main(void)
{
   gss_import_name(0, 0, gss_nt_service_name, 0);
   gss_init_sec_context(0,0,0,0,0,0,0,0,0,0,0,0,0);
   return 0;
}
!
   then
      :
   else
      feat_bail_required GSSAPI
   fi
else
   echo '/* WANT_GSSAPI=0 */' >> ${h}
fi # feat_yes GSSAPI

if feat_yes NETRC; then
   echo '#define HAVE_NETRC' >> ${h}
else
   echo '/* WANT_NETRC=0 */' >> ${h}
fi

if feat_yes AGENT; then
   echo '#define HAVE_AGENT' >> ${h}
else
   echo '/* WANT_AGENT=0 */' >> ${h}
fi

if feat_yes IDNA; then
   if link_check idna 'for GNU Libidn' '#define HAVE_IDNA' '-lidn' << \!
#include <idna.h>
#include <idn-free.h>
#include <stringprep.h>
int main(void)
{
   char *utf8, *idna_ascii, *idna_utf8;
   utf8 = stringprep_locale_to_utf8("does.this.work");
   if (idna_to_ascii_8z(utf8, &idna_ascii, IDNA_USE_STD3_ASCII_RULES)
         != IDNA_SUCCESS)
      return 1;
   idn_free(idna_ascii);
   /* (Rather link check only here) */
   idna_utf8 = stringprep_convert(idna_ascii, "UTF-8", "de_DE");
   return 0;
}
!
   then
      :
   else
      feat_bail_required IDNA
   fi
else
   echo '/* WANT_IDNA=0 */' >> ${h}
fi

if feat_yes IMAP_SEARCH; then
   echo '#define HAVE_IMAP_SEARCH' >> ${h}
else
   echo '/* WANT_IMAP_SEARCH=0 */' >> ${h}
fi

if feat_yes REGEX; then
   if link_check regex 'for regular expressions' '#define HAVE_REGEX' << \!
#include <regex.h>
#include <stdlib.h>
int main(void)
{
   int status;
   regex_t re;
   if (regcomp(&re, ".*bsd", REG_EXTENDED | REG_ICASE | REG_NOSUB) != 0)
      return 1;
   status = regexec(&re, "plan9", 0,NULL, 0);
   regfree(&re);
   return !(status == REG_NOMATCH);
}
!
   then
      :
   else
      feat_bail_required REGEX
   fi
else
   echo '/* WANT_REGEX=0 */' >> ${h}
fi

if feat_yes READLINE; then
   __edrdlib() {
      link_check readline "for readline(3) (${1})" \
         '#define HAVE_READLINE' "${1}" << \!
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
int main(void)
{
   char *rl;
   HISTORY_STATE *hs;
   HIST_ENTRY **he;
   int i;
   using_history();
   read_history("");
   stifle_history(242);
   rl = readline("Enter a line:");
   if (rl && *rl)
      add_history(rl);
   write_history("");
   rl_extend_line_buffer(10);
   rl_point = rl_end = 10;
   rl_pre_input_hook = (rl_hook_func_t*)NULL;
   rl_forced_update_display();
   clear_history();
   hs = history_get_history_state();
   i = hs->length;
   he = history_list();
   if (i > 0)
      rl = he[0]->line;
   rl_free_line_state();
   rl_cleanup_after_signal();
   rl_reset_after_signal();
   return 0;
}
!
   }

   __edrdlib -lreadline ||
      __edrdlib '-lreadline -ltermcap' || feat_bail_required READLINE
   [ -n "${have_readline}" ] && WANT_TABEXPAND=1
fi

if feat_yes EDITLINE && [ -z "${have_readline}" ]; then
   __edlib() {
      link_check editline "for editline(3) (${1})" \
         '#define HAVE_EDITLINE' "${1}" << \!
#include <histedit.h>
static char * getprompt(void) { return (char*)"ok"; }
int main(void)
{
   EditLine *el_el = el_init("TEST", stdin, stdout, stderr);
   HistEvent he;
   History *el_hcom = history_init();
   history(el_hcom, &he, H_SETSIZE, 242);
   el_set(el_el, EL_SIGNAL, 0);
   el_set(el_el, EL_TERMINAL, NULL);
   el_set(el_el, EL_HIST, &history, el_hcom);
   el_set(el_el, EL_EDITOR, "emacs");
   el_set(el_el, EL_PROMPT, &getprompt);
   el_source(el_el, NULL);
   history(el_hcom, &he, H_GETSIZE);
   history(el_hcom, &he, H_CLEAR);
   el_end(el_el);
   /* TODO add loader and addfn checks */
   history_end(el_hcom);
   return 0;
}
!
   }

   __edlib -ledit ||
      __edlib '-ledit -ltermcap' || feat_bail_required EDITLINE
   [ -n "${have_editline}" ] && WANT_TABEXPAND=0
fi

if feat_yes NCL && [ -z "${have_editline}" ] && [ -z "${have_readline}" ] &&\
      [ -n "${have_c90amend1}" ]; then
   have_ncl=1
   echo '#define HAVE_NCL' >> ${h}
else
   feat_bail_required NCL
   echo '/* WANT_{READLINE,EDITLINE,NCL}=0 */' >> ${h}
fi

# Generic have-a-command-line-editor switch for those who need it below
if [ -n "${have_ncl}" ] || [ -n "${have_editline}" ] ||\
      [ -n "${have_readline}" ]; then
   have_cle=1
fi

if [ -n "${have_cle}" ] && feat_yes HISTORY; then
   echo '#define HAVE_HISTORY' >> ${h}
else
   echo '/* WANT_HISTORY=0 */' >> ${h}
fi

if [ -n "${have_cle}" ] && feat_yes TABEXPAND; then
   echo '#define HAVE_TABEXPAND' >> ${h}
else
   echo '/* WANT_TABEXPAND=0 */' >> ${h}
fi

if feat_yes SPAM; then
   echo '#define HAVE_SPAM' >> ${h}
   if command -v spamc >/dev/null 2>&1; then
      echo "#define SPAMC_PATH \"`command -v spamc`\"" >> ${h}
   fi
else
   echo '/* WANT_SPAM=0 */' >> ${h}
fi

if feat_yes DOCSTRINGS; then
   echo '#define HAVE_DOCSTRINGS' >> ${h}
else
   echo '/* WANT_DOCSTRINGS=0 */' >> ${h}
fi

if feat_yes QUOTE_FOLD &&\
      [ -n "${have_c90amend1}" ] && [ -n "${have_wcwidth}" ]; then
   echo '#define HAVE_QUOTE_FOLD' >> ${h}
else
   echo '/* WANT_QUOTE_FOLD=0 */' >> ${h}
fi

if feat_yes COLOUR; then
   echo '#define HAVE_COLOUR' >> ${h}
else
   echo '/* WANT_COLOUR=0 */' >> ${h}
fi

if feat_yes MD5; then
   echo '#define HAVE_MD5' >> ${h}
else
   echo '/* WANT_MD5=0 */' >> ${h}
fi

## Summarizing ##

# Since we cat(1) the content of those to cc/"ld", convert them to single line
squeeze_em() {
   < "${1}" > "${2}" ${awk} \
   'BEGIN {ORS = " "} /^[^#]/ {print} {next} END {ORS = ""; print "\n"}'
}
${rm} -f ${tmp}
squeeze_em ${inc} ${tmp}
${mv} ${tmp} ${inc}
squeeze_em ${lib} ${tmp}
${mv} ${tmp} ${lib}

# config.h
${mv} ${h} ${tmp}
printf '#ifndef _CONFIG_H\n# define _CONFIG_H\n' > ${h}
${cat} ${tmp} >> ${h}

printf '\n/* The "feature string", for "simplicity" and lex.c */\n' >> ${h}
printf '#ifdef _MAIN_SOURCE\n' >> ${h}
printf '# ifdef HAVE_AMALGAMATION\nstatic\n# endif\n' >> ${h}
printf 'char const features[] = "MIME"\n' >> ${h}
printf '# ifdef HAVE_SETLOCALE\n   ",LOCALES"\n# endif\n' >> ${h}
printf '# ifdef HAVE_C90AMEND1\n   ",MULTIBYTE CHARSETS"\n# endif\n' >> ${h}
printf '# ifdef HAVE_NL_LANGINFO\n   ",TERMINAL CHARSET"\n# endif\n' >> ${h}
printf '# ifdef HAVE_ICONV\n   ",ICONV"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SOCKETS\n   ",NETWORK"\n# endif\n' >> ${h}
printf '# ifdef HAVE_IPV6\n   ",IPv6"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SSL\n   ",S/MIME,SSL/TLS"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SMTP\n   ",SMTP"\n# endif\n' >> ${h}
printf '# ifdef HAVE_POP3\n   ",POP3"\n# endif\n' >> ${h}
printf '# ifdef HAVE_IMAP\n   ",IMAP"\n# endif\n' >> ${h}
printf '# ifdef HAVE_GSSAPI\n   ",GSS-API"\n# endif\n' >> ${h}
printf '# ifdef HAVE_NETRC\n   ",NETRC"\n# endif\n' >> ${h}
printf '# ifdef HAVE_AGENT\n   ",AGENT"\n# endif\n' >> ${h}
printf '# ifdef HAVE_IDNA\n   ",IDNA"\n# endif\n' >> ${h}
printf '# ifdef HAVE_IMAP_SEARCH\n   ",IMAP-searches"\n# endif\n' >> ${h}
printf '# ifdef HAVE_REGEX\n   ",REGEX"\n# endif\n' >> ${h}
printf '# ifdef HAVE_READLINE\n   ",READLINE"\n# endif\n' >> ${h}
printf '# ifdef HAVE_EDITLINE\n   ",EDITLINE"\n# endif\n' >> ${h}
printf '# ifdef HAVE_NCL\n   ",NCL"\n# endif\n' >> ${h}
printf '# ifdef HAVE_TABEXPAND\n   ",TABEXPAND"\n# endif\n' >> ${h}
printf '# ifdef HAVE_HISTORY\n   ",HISTORY"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SPAM\n   ",SPAM"\n# endif\n' >> ${h}
printf '# ifdef HAVE_DOCSTRINGS\n   ",DOCSTRINGS"\n# endif\n' >> ${h}
printf '# ifdef HAVE_QUOTE_FOLD\n   ",QUOTE-FOLD"\n# endif\n' >> ${h}
printf '# ifdef HAVE_COLOUR\n   ",COLOUR"\n# endif\n' >> ${h}
printf '# ifdef HAVE_DEBUG\n   ",DEBUG"\n# endif\n' >> ${h}
printf ';\n#endif /* _MAIN_SOURCE */\n' >> ${h}

printf '#endif /* _CONFIG_H */\n' >> ${h}
${rm} -f ${tmp}

# Create the real mk.mk
${rm} -rf ${tmp0}.* ${tmp0}*
printf 'OBJ_SRC = ' >> ${mk}
if feat_no AMALGAMATION; then
   echo *.c >> ${mk}
   echo 'OBJ_DEP =' >> ${mk}
else
   j=`echo "${src}" | sed 's/^.\///'`
   echo "${j}" >> ${mk}
   printf 'OBJ_DEP = main.c ' >> ${mk}
   printf '#define _MAIN_SOURCE\n' >> ${src}
   printf '#include "nail.h"\n#include "main.c"\n' >> ${src}
   for i in *.c; do
      if [ "${i}" = "${j}" ] || [ "${i}" = main.c ]; then
         continue
      fi
      printf "${i} " >> ${mk}
      printf "#include \"${i}\"\n" >> ${src}
   done
   echo >> ${mk}
fi

echo "LIBS = `${cat} ${lib}`" >> ${mk}
echo "INCLUDES = `${cat} ${inc}`" >> ${mk}
echo >> ${mk}
${cat} ./mk-mk.in >> ${mk}

## Finished! ##

${cat} > ${tmp2}.c << \!
#include "config.h"
#ifdef HAVE_NL_LANGINFO
# include <langinfo.h>
#endif
:
:The following optional features are enabled:
#ifdef HAVE_SETLOCALE
: + Locale support: Printable characters depend on the environment
# ifdef HAVE_C90AMEND1
: + Multibyte character support
# endif
# ifdef HAVE_NL_LANGINFO
: + Automatic detection of terminal character set
# endif
#endif
#ifdef HAVE_ICONV
: + Character set conversion using iconv()
#endif
#ifdef HAVE_SOCKETS
: + Network support
#endif
#ifdef HAVE_IPV6
: + Support for Internet Protocol v6 (IPv6)
#endif
#ifdef HAVE_SSL
# ifdef HAVE_OPENSSL
: + S/MIME and SSL/TLS (OpenSSL)
# endif
#endif
#ifdef HAVE_SMTP
: + SMTP protocol
#endif
#ifdef HAVE_POP3
: + POP3 protocol
#endif
#ifdef HAVE_IMAP
: + IMAP protocol
#endif
#ifdef HAVE_GSSAPI
: + GSS-API authentication
#endif
#ifdef HAVE_NETRC
: + .netrc file support
#endif
#ifdef HAVE_AGENT
: + Password query through agent
#endif
#ifdef HAVE_IDNA
: + IDNA (internationalized domain names for applications) support
#endif
#ifdef HAVE_IMAP_SEARCH
: + IMAP-style search expressions
#endif
#ifdef HAVE_REGEX
: + Regular expression support (searches, conditional expressions etc.)
#endif
#if defined HAVE_READLINE || defined HAVE_EDITLINE || defined HAVE_NCL
: + Command line editing
# ifdef HAVE_TABEXPAND
: + + Tabulator expansion
# endif
# ifdef HAVE_HISTORY
: + + History management
# endif
#endif
#ifdef HAVE_SPAM
: + Interaction with spam filters
#endif
#ifdef HAVE_DOCSTRINGS
: + Documentation summary strings
#endif
#ifdef HAVE_QUOTE_FOLD
: + Extended *quote-fold*ing
#endif
#ifdef HAVE_COLOUR
: + Coloured message display (simple)
#endif
:
:The following optional features are disabled:
#ifndef HAVE_SETLOCALE
: - Locale support: Only ASCII characters are recognized
#endif
# ifndef HAVE_C90AMEND1
: - Multibyte character support
# endif
# ifndef HAVE_NL_LANGINFO
: - Automatic detection of terminal character set
# endif
#ifndef HAVE_ICONV
: - Character set conversion using iconv()
#endif
#ifndef HAVE_SOCKETS
: - Network support
#endif
#ifndef HAVE_IPV6
: - Support for Internet Protocol v6 (IPv6)
#endif
#ifndef HAVE_SSL
: - S/MIME and SSL/TLS
#endif
#ifndef HAVE_SMTP
: - SMTP protocol
#endif
#ifndef HAVE_POP3
: - POP3 protocol
#endif
#ifndef HAVE_IMAP
: - IMAP protocol
#endif
#ifndef HAVE_GSSAPI
: - GSS-API authentication
#endif
#ifndef HAVE_NETRC
: - .netrc file support
#endif
#ifndef HAVE_AGENT
: - Password query through agent
#endif
#ifndef HAVE_IDNA
: - IDNA (internationalized domain names for applications) support
#endif
#ifndef HAVE_IMAP_SEARCH
: - IMAP-style search expressions
#endif
#ifndef HAVE_REGEX
: - Regular expression support
#endif
#if !defined HAVE_READLINE && !defined HAVE_EDITLINE && !defined HAVE_NCL
: - Command line editing and history
#endif
#ifndef HAVE_SPAM
: - Interaction with spam filters
#endif
#ifndef HAVE_DOCSTRINGS
: - Documentation summary strings
#endif
#ifndef HAVE_QUOTE_FOLD
: - Extended *quote-fold*ing
#endif
#ifndef HAVE_COLOUR
: - Coloured message display (simple)
#endif
:
#if !defined HAVE_WORDEXP || !defined HAVE_SNPRINTF || !defined HAVE_FCHDIR ||\
      defined HAVE_DEBUG || defined HAVE_DEVEL
:Remarks:
# ifndef HAVE_WORDEXP
: . WARNING: the function wordexp(3) could not be found.
: _ This means that echo(1) will be used via the sh(1)ell in order
: _ to expand shell meta characters in filenames, which is a potential
: _ security hole.  Consider to either upgrade your system or set the
: _ *SHELL* variable to some safe(r) wrapper script.
# endif
# ifndef HAVE_SNPRINTF
: . The function snprintf(3) could not be found. We will use an internal
: _ sprintf() instead. This might overflow buffers if input values are
: _ larger than expected. Use the resulting binary with care or update
: _ your system environment and start the configuration process again.
# endif
# ifndef HAVE_FCHDIR
: . The function fchdir(2) could not be found. We will use chdir(2)
: _ instead. This is not a problem unless the current working
: _ directory of mailx is moved while the IMAP cache is used.
# endif
# ifdef HAVE_DEBUG
: . Debug enabled binary: not meant to be used by end-users: THANKS!
# endif
# ifdef HAVE_DEVEL
: . Computers don't blunder.
# endif
#endif /* Remarks */
:
:Setup:
: . System-wide resource file: SYSCONFRC
: . bindir: BINDIR, mandir: MANDIR
: . sendmail(1): SENDMAIL (argv[0] = SENDMAIL_PROGNAME)
: . $MAILSPOOL: MAILSPOOL
:
!

${make} -f ${makefile} ${tmp2}.x
< ${tmp2}.x >&5 ${sed} -e '/^[^:]/d; /^$/d; s/^://'

# s-it-mode
