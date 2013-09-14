#!/bin/sh -
#@ Please see `INSTALL' and `conf.rc' instead.

# Predefined CONFIG= urations take precedence over anything else
if [ -n "${CONFIG}" ]; then
   case ${CONFIG} in
   MINIMAL)
      WANT_SOCKETS=0
      WANT_IDNA=0
      WANT_LINE_EDITOR=0
      WANT_QUOTE_FOLD=0
      WANT_DOCSTRINGS=0
      WANT_SPAM=0
      ;;
   NETSEND)
      WANT_IMAP=0
      WANT_POP3=0
      WANT_EDITLINE=0
      WANT_QUOTE_FOLD=0
      WANT_DOCSTRINGS=0
      WANT_SPAM=0
      ;;
   *)
      echo >&2 "Unknown CONFIG= setting: ${CONFIG}"
      echo >&2 'Possible values: MINIMAL, NETSEND'
      exit 1
   esac
fi

option_update() {
   if nwantfeat SOCKETS; then
      WANT_IPV6=0 WANT_SSL=0
      WANT_IMAP=0 WANT_GSSAPI=0 WANT_POP3=0 WANT_SMTP=0
   fi
   if nwantfeat IMAP && nwantfeat POP3 && nwantfeat SMTP; then
      WANT_SOCKETS=0 WANT_IPV6=0 WANT_SSL=0
   fi
   if nwantfeat IMAP; then
      WANT_GSSAPI=0
   fi
   # If we don't need MD5 except for producing boundary and message-id strings,
   # leave it off, plain old srand(3) should be enough for that purpose.
   if nwantfeat SOCKETS; then
      WANT_MD5=0
   fi
   if nwantfeat LINE_EDITOR; then
      WANT_EDITLINE=0 WANT_EDITLINE_READLINE=0
   fi
   if nwantfeat EDITLINE; then
      WANT_EDITLINE_READLINE=0
   fi
}

make="${MAKE:-make}"

##  --  >8  --  8<  --  ##

## First of all, create new configuration and check wether it changed ##

conf=./conf.rc
lst=./config.lst
h=./config.h
mk=./mk.mk

newlst=./config.lst-new
newmk=./config.mk-new
newh=./config.h-new
tmp0=___tmp
tmp=./${tmp0}1$$

# Only incorporate what wasn't overwritten from command line / CONFIG
< ${conf} sed -e '/^[ \t]*#/d' -e '/^$/d' -e 's/[ \t]*$//' > ${tmp}
while read line; do
   i=`echo ${line} | sed -e 's/=.*$//'`
   eval j="\$${i}" jx="\${${i}+x}"
   [ -n "${j}" ] && continue
   [ "${jx}" = x ] && continue
   eval ${line}
done < ${tmp}

wantfeat() {
   eval i=\$WANT_${1}
   [ "${i}" = "1" ]
}
nwantfeat() {
   eval i=\$WANT_${1}
   [ "${i}" != "1" ]
}

option_update

# (No function since some shells loose non-exported variables in traps)
trap "rm -f ${tmp} ${newlst} ${newmk} ${newh}; exit" 1 2 15
trap "rm -f ${tmp} ${newlst} ${newmk} ${newh}" 0
rm -f ${newlst} ${newmk} ${newh}

while read line; do
   i=`echo ${line} | sed -e 's/=.*$//'`
   eval j=\$${i}
   printf "${i}=\"${j}\"\n" >> ${newlst}
   if [ -z "${j}" ] || [ "${j}" = 0 ]; then
      printf "/*#define ${i}*/\n" >> ${newh}
   elif [ "${j}" = 1 ]; then
      printf "#define ${i}\n" >> ${newh}
   else
      printf "#define ${i} \"${j}\"\n" >> ${newh}
   fi
   printf "${i} = ${j}\n" >> ${newmk}
done < ${tmp}
printf "#define UAGENT \"${SID}${NAIL}\"\n" >> ${newh}
printf "UAGENT = ${SID}${NAIL}\n" >> ${newmk}

if [ -f ${lst} ] && "${CMP}" ${newlst} ${lst} >/dev/null 2>&1; then
   exit 0
fi
[ -f ${lst} ] && echo 'configuration updated..' || echo 'shiny configuration..'

mv -f ${newlst} ${lst}
mv -f ${newh} ${h}
mv -f ${newmk} ${mk}

## Compile and link checking ##

tmp2=./${tmp0}2$$
tmp3=./${tmp0}3$$
log=./config.log
lib=./config.lib
inc=./config.inc
makefile=./config.mk

# (No function since some shells loose non-exported variables in traps)
trap "rm -f ${lst} ${h} ${mk} ${lib} ${inc} ${makefile}; exit" 1 2 15
trap "rm -rf ${tmp0}.* ${tmp0}* ${makefile}" 0

exec 5>&2 > ${log} 2>&1
: > ${lib}
: > ${inc}
cat > ${makefile} << \!
.SUFFIXES: .o .c .x .y
.c.o:
	$(CC) $(XINCS) -c $<

.c.x:
	$(CC) $(XINCS) -E $< >$@

.c:
	$(CC) $(XINCS) -o $@ $< $(XLIBS)

.y: ;
!

msg() {
   fmt=$1

   shift
   printf "*** ${fmt}\\n" "${@}"
   printf "${fmt}" "${@}" >&5
}

_check_preface() {
   variable=$1 topic=$2 define=$3

   echo '**********'
   msg "checking ${topic} ... "
   echo "/* checked ${topic} */" >> ${h}
   rm -f ${tmp} ${tmp}.o
   echo '*** test program is'
   tee ${tmp}.c
   #echo '*** the preprocessor generates'
   #${make} -f ${makefile} ${tmp}.x
   #cat ${tmp}.x
   echo '*** results are'
}

compile_check() {
   variable=$1 topic=$2 define=$3

   _check_preface "${variable}" "${topic}" "${define}"

   if ${make} -f ${makefile} XINCS="${INCS}" ./${tmp}.o &&
         [ -f ./${tmp}.o ]; then
      msg "okay\\n"
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
      msg "okay\\n"
      echo "${define}" >> ${h}
      LIBS="${LIBS} ${libs}"
      echo "${libs}" >> ${lib}
      echo "$2: ${libs}"
      INCS="${INCS} ${incs}"
      echo "${incs}" >> ${inc}
      echo "$2: ${incs}"
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
      [ "${i}" = '/usr/pkg/include' ] && continue
      INCS="${INCS} -I${i}"
   done
fi
[ -d /usr/pkg/include ] && INCS="${INCS} -I/usr/pkg/include"
echo "${INCS}" >> ${inc}

if [ -n "${LD_LIBRARY_PATH}" ]; then
   i=${IFS}
   IFS=:
   set -- ${LD_LIBRARY_PATH}
   IFS=${i}
   # for i; do -- new in POSIX Issue 7 + TC1
   for i
   do
      [ "${i}" = '/usr/pkg/lib' ] && continue
      LIBS="${LIBS} -L${i}"
   done
fi
[ -d /usr/pkg/lib ] && LIBS="${LIBS} -L/usr/pkg/lib"
echo "${LIBS}" >> ${lib}

##

# Better set _GNU_SOURCE (if we are on Linux only?)
# Fixes compilation on Slackware 14 + (with at least clang(1)).
# Since i've always defined this on GNU/Linux, i'm even surprised it works
# without!!  Didn't check this yet (and TinyCore uses different environment).
echo '#define _GNU_SOURCE' >> ${h}

link_check hello 'if a hello world program can be built' <<\! || {\
   echo >&5 'This oooops is most certainly not related to me.';\
   echo >&5 "Read the file ${log} and check your compiler environment.";\
   rm ${lst} ${h} ${mk}; exit 1;\
}
#include <stdio.h>

int main(int argc, char *argv[])
{
   (void)argc;
   (void)argv;
   puts("hello world");
   return 0;
}
!

link_check snprintf 'for snprintf()' '#define HAVE_SNPRINTF' << \!
#include <stdio.h>
int main(void)
{
   char	b[20];
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

link_check wctype 'for wctype functionality' '#define HAVE_WCTYPE_H' << \!
#include <wctype.h>
int main(void)
{
   iswprint(L'c');
   towupper(L'c');
   return 0;
}
!

link_check wcwidth 'for wcwidth() ' '#define HAVE_WCWIDTH' << \!
#include <wchar.h>
int main(void)
{
   wcwidth(L'c');
   return 0;
}
!

link_check mbtowc 'for mbtowc()' '#define HAVE_MBTOWC' << \!
#include <stdlib.h>
int main(void)
{
   wchar_t	wc;
   mbtowc(&wc, "x", 1);
   return 0;
}
!

link_check mbrtowc 'for mbrtowc()' '#define HAVE_MBRTOWC' << \!
#include <wchar.h>
int main(void)
{
   wchar_t	wc;
   mbrtowc(&wc, "x", 1, NULL);
   return 0;
}
!

link_check mblen 'for mblen()' '#define HAVE_MBLEN' << \!
#include <stdlib.h>
int main(void)
{
   return mblen("\0", 1) == 0;
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

link_check nl_langinfo 'for nl_langinfo()' '#define HAVE_NL_LANGINFO' << \!
#include <langinfo.h>
int main(void)
{
   nl_langinfo(DAY_1);
   return 0;
}
!

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
   wordexp((char *)0, (wordexp_t *)0, 0);
   return 0;
}
!

##

if wantfeat ASSERTS; then
   echo '#define HAVE_ASSERTS' >> ${h}
fi

if nwantfeat NOALLOCA; then
   # Due to NetBSD PR lib/47120 it seems best not to use non-cc-builtin
   # versions of alloca(3) since modern compilers just can't be trusted
   # not to overoptimize and silently break some code
   link_check alloca 'for __builtin_alloca()' \
      '#define HAVE_ALLOCA __builtin_alloca' << \!
int main(void)
{
   void *vp = __builtin_alloca(1);
   return (!! vp);
}
!
fi

if nwantfeat NOGETOPT; then
   link_check getopt 'for getopt()' '#define HAVE_GETOPT' << \!
#include <unistd.h>
int main(int argc, char **argv)
{
#if defined __GLIBC__ || defined __linux__
   Argument and option reordering is not a desired feature.
#else
   getopt(argc, argv, "oPt");
#endif
   return (((long)optarg + optind) & 0x7F);
}
!
fi

##

if wantfeat ICONV; then
   cat > ${tmp2}.c << \!
#include <iconv.h>
int main(void)
{
   iconv_t	id;

   id = iconv_open("foo", "bar");
   return 0;
}
!
   < ${tmp2}.c link_check iconv 'for iconv functionality' \
         '#define HAVE_ICONV' ||
      < ${tmp2}.c link_check iconv \
         'for iconv functionality in libiconv' \
         '#define HAVE_ICONV' '-liconv'
else
   echo '/* WANT_ICONV=0 */' >> ${h}
fi # wantfeat ICONV

if wantfeat SOCKETS; then
   compile_check arpa_inet_h 'for <arpa/inet.h>' \
      '#define HAVE_ARPA_INET_H' << \!
#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
!

   cat > ${tmp2}.c << \!
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
   struct sockaddr	s;
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
      WANT_SOCKETS=0

   # XXX Shouldn't it be a hard error if there is no socket support, then?
   [ ${WANT_SOCKETS} -eq 1 ] ||
      WANT_IPV6=0 WANT_SSL=0 \
      WANT_IMAP=0 WANT_GSSAPI=0 WANT_POP3=0 WANT_SMTP=0
else
   echo '/* WANT_SOCKETS=0 */' >> ${h}
fi # wantfeat SOCKETS

wantfeat SOCKETS &&
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

wantfeat SOCKETS && [ -n "${have_setsockopt}" ] &&
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

wantfeat SOCKETS && [ -n "${have_setsockopt}" ] &&
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

if wantfeat IPV6; then
   link_check ipv6 'for IPv6 functionality' '#define HAVE_IPV6' << \!
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
   struct addrinfo	a, *ap;
   getaddrinfo("foo", "0", &a, &ap);
   return 0;
}
!
else
   echo '/* WANT_IPV6=0 */' >> ${h}
fi # wantfeat IPV6

if wantfeat IMAP; then
   echo "#define HAVE_IMAP" >> ${h}
else
   echo '/* WANT_IMAP=0 */' >> ${h}
fi

if wantfeat POP3; then
   echo "#define HAVE_POP3" >> ${h}
else
   echo '/* WANT_POP3=0 */' >> ${h}
fi

if wantfeat SMTP; then
   echo "#define HAVE_SMTP" >> ${h}
else
   echo '/* WANT_SMTP=0 */' >> ${h}
fi

if wantfeat SSL; then
   link_check openssl 'for sufficiently recent OpenSSL' \
      '#define HAVE_SSL
      #define HAVE_OPENSSL' '-lssl -lcrypto' << \!
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

int main(void)
{
   SSLv23_client_method();
   PEM_read_PrivateKey(0, 0, 0, 0);
   return 0;
}
!

   if [ "${have_openssl}" = 'yes' ]; then
      compile_check stack_of 'for STACK_OF()' \
         '#define HAVE_STACK_OF' << \!
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

int main(void)
{
   STACK_OF(GENERAL_NAME)	*gens = NULL;
   printf("%p", gens);	/* to make it used */
   SSLv23_client_method();
   PEM_read_PrivateKey(0, 0, 0, 0);
   return 0;
}
!
   fi

else
   echo '/* WANT_SSL=0 */' >> ${h}
fi # wantfeat SSL

if wantfeat GSSAPI; then
   cat > ${tmp2}.c << \!
#include <gssapi/gssapi.h>

int main(void)
{
   gss_import_name(0, 0, GSS_C_NT_HOSTBASED_SERVICE, 0);
   gss_init_sec_context(0,0,0,0,0,0,0,0,0,0,0,0,0);
   return 0;
}
!
   sed -e '1s/gssapi\///' < ${tmp2}.c > ${tmp3}.c

   if command -v krb5-config >/dev/null 2>&1; then
      i=`command -v krb5-config`
      GSSAPI_LIBS="`CFLAGS= ${i} --libs gssapi`"
      GSSAPI_INCS="`CFLAGS= ${i} --cflags`"
      i='for GSSAPI via krb5-config(1)'
   else
      GSSAPI_LIBS='-lgssapi'
      GSSAPI_INCS=
      i='for GSSAPI in gssapi/gssapi.h, libgssapi'
   fi
   < ${tmp2}.c link_check gssapi \
         "${i}" '#define HAVE_GSSAPI' \
         "${GSSAPI_LIBS}" "${GSSAPI_INCS}" ||\
      < ${tmp3}.c link_check gssapi \
         'for GSSAPI in gssapi.h, libgssapi' \
         '#define HAVE_GSSAPI
         #define	GSSAPI_REG_INCLUDE' \
         '-lgssapi' ||\
      < ${tmp2}.c link_check gssapi 'for GSSAPI in libgssapi_krb5' \
         '#define HAVE_GSSAPI' \
         '-lgssapi_krb5' ||\
      < ${tmp3}.c link_check gssapi \
         'for GSSAPI in libgssapi, OpenBSD-style (pre 5.3)' \
         '#define HAVE_GSSAPI
         #define	GSSAPI_REG_INCLUDE' \
         '-lgssapi -lkrb5 -lcrypto' \
         '-I/usr/include/kerberosV' ||\
      < ${tmp2}.c link_check gssapi 'for GSSAPI in libgss' \
         '#define HAVE_GSSAPI' \
         '-lgss' ||\
      link_check gssapi 'for GSSAPI in libgssapi_krb5, old-style' \
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
else
   echo '/* WANT_GSSAPI=0 */' >> ${h}
fi # wantfeat GSSAPI

if wantfeat IDNA; then
   link_check idna 'for GNU Libidn' '#define HAVE_IDNA' '-lidn' << \!
#include <idna.h>
#include <stringprep.h>
int main(void)
{
   char *utf8, *idna_ascii, *idna_utf8;
   utf8 = stringprep_locale_to_utf8("does.this.work");
   if (idna_to_ascii_8z(utf8, &idna_ascii, IDNA_USE_STD3_ASCII_RULES)
         != IDNA_SUCCESS)
      return 1;
   /* (Rather link check only here) */
   idna_utf8 = stringprep_convert(idna_ascii, "UTF-8", "de_DE");
   return 0;
}
!
else
   echo '/* WANT_IDNA=0 */' >> ${h}
fi # wantfeat IDNA

if wantfeat EDITLINE_READLINE; then
   link_check readline 'for readline(3) compatible editline(3)' \
      '#define HAVE_LINE_EDITOR
      #define HAVE_READLINE' '-lreadline -ltermcap' << \!
#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
int main(void)
{
   char *rl;
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

   rl_free_line_state();
   rl_cleanup_after_signal();
   rl_reset_after_signal();
   return 0;
}
!
fi

if wantfeat EDITLINE && [ -z "${have_readline}" ]; then
   link_check editline 'for editline(3)' \
      '#define HAVE_LINE_EDITOR
      #define HAVE_EDITLINE' '-ledit' << \!
#include <histedit.h>
static char * getprompt(void) { return (char*)"ok"; }
int main(void)
{
   HistEvent he;
   EditLine *el_el = el_init("TEST", stdin, stdout, stderr);
   History *el_hcom = history_init();
   history(el_hcom, &he, H_SETSIZE, 242);
   el_set(el_el, EL_SIGNAL, 0);
   el_set(el_el, EL_TERMINAL, NULL);
   el_set(el_el, EL_HIST, &history, el_hcom);
   el_set(el_el, EL_EDITOR, "emacs");
   el_set(el_el, EL_PROMPT, &getprompt);
   el_source(el_el, NULL);
   el_end(el_el);
   /* TODO add loader and addfn checks */
   history_end(el_hcom);
   return 0;
}
!
fi

if wantfeat LINE_EDITOR && [ -z "${have_editline}" ] &&\
      [ -z "${have_readline}" ] &&\
      [ -n "${have_mbrtowc}" ] && [ -n "${have_wctype}" ]; then
   echo "#define HAVE_LINE_EDITOR" >> ${h}
else
   echo '/* WANT_{LINE_EDITOR,EDITLINE,EDITLINE_READLINE}=0 */' >> ${h}
fi

if wantfeat QUOTE_FOLD &&\
      [ -n "${have_mbrtowc}" ] && [ -n "${have_wcwidth}" ]; then
   echo "#define HAVE_QUOTE_FOLD" >> ${h}
else
   echo '/* WANT_QUOTE_FOLD=0 */' >> ${h}
fi

if wantfeat DOCSTRINGS; then
   echo "#define HAVE_DOCSTRINGS" >> ${h}
else
   echo '/* WANT_DOCSTRINGS=0 */' >> ${h}
fi

if wantfeat SPAM; then
   echo "#define HAVE_SPAM" >> ${h}
   if command -v spamc >/dev/null 2>&1; then
      echo "#define SPAMC_PATH \"`command -v spamc`\"" >> ${h}
   fi
else
   echo '/* WANT_SPAM=0 */' >> ${h}
fi

if wantfeat MD5; then
   echo "#define HAVE_MD5" >> ${h}
else
   echo '/* WANT_MD5=0 */' >> ${h}
fi

## Summarizing ##

# Since we cat(1) the content of those to cc/"ld", convert them to single line
squeeze_em() {
   < "${1}" > "${2}" awk \
   'BEGIN {ORS = " "} /^[^#]/ {print} {next} END {ORS = ""; print "\n"}'
}
rm -f "${tmp}"
squeeze_em "${inc}" "${tmp}"
mv "${tmp}" "${inc}"
squeeze_em "${lib}" "${tmp}"
mv "${tmp}" "${lib}"

# Create the real mk.mk
rm -rf ${tmp0}.* ${tmp0}*
printf 'OBJ = ' >> ${mk}
for i in *.c; do
   printf "`basename ${i} .c`.o " >> ${mk}
done
echo >> ${mk}
echo "LIBS = `cat ${lib}`" >> ${mk}
echo "INCLUDES = `cat ${inc}`" >> ${mk}
echo >> ${mk}
cat ./mk-mk.in >> ${mk}

## Finished! ##

cat > ${tmp2}.c << \!
#include "config.h"
#ifdef HAVE_NL_LANGINFO
#include <langinfo.h>
#endif
:
:The following optional features are enabled:
#ifdef HAVE_ICONV
: + Character set conversion using iconv()
#endif
#ifdef HAVE_SETLOCALE
: + Locale support: Printable characters depend on the environment
# if defined HAVE_MBTOWC && defined HAVE_WCTYPE_H
: + Multibyte character support
# endif
# if defined HAVE_NL_LANGINFO && defined CODESET
: + Automatic detection of terminal character set
# endif
#endif
#ifdef HAVE_SOCKETS
: + Network support
#endif
#ifdef HAVE_IPV6
: + Support for Internet Protocol v6 (IPv6)
#endif
#ifdef HAVE_OPENSSL
: + S/MIME and SSL/TLS using OpenSSL
#endif
#ifdef HAVE_IMAP
: + IMAP protocol
#endif
#ifdef HAVE_GSSAPI
: + IMAP GSSAPI authentication
#endif
#ifdef HAVE_POP3
: + POP3 protocol
#endif
#ifdef HAVE_SMTP
: + SMTP protocol
#endif
#ifdef HAVE_SPAM
: + Interaction with spam filters
#endif
#ifdef HAVE_IDNA
: + IDNA (internationalized domain names for applications) support
#endif
#ifdef HAVE_LINE_EDITOR
: + Command line editing and history
#endif
#if 0
TODO disabled for v14.4, since not multibyte aware
ifdef HAVE_QUOTE_FOLD
: + Extended *quote-fold*ing
#endif
:
:The following optional features are disabled:
#ifndef	HAVE_ICONV
: - Character set conversion using iconv()
#endif
#ifndef	HAVE_SETLOCALE
: - Locale support: Only ASCII characters are recognized
#endif
#if ! defined HAVE_SETLOCALE || ! defined HAVE_MBTOWC || !defined HAVE_WCTYPE_H
: - Multibyte character support
#endif
#if ! defined HAVE_SETLOCALE || ! defined HAVE_NL_LANGINFO || ! defined CODESET
: - Automatic detection of terminal character set
#endif
#ifndef	HAVE_SOCKETS
: - Network support
#endif
#ifndef	HAVE_IPV6
: - Support for Internet Protocol v6 (IPv6)
#endif
#if ! defined HAVE_SSL
: - SSL/TLS (network transport authentication and encryption)
#endif
#ifndef HAVE_IMAP
: - IMAP protocol
#endif
#ifndef	HAVE_GSSAPI
: - IMAP GSSAPI authentication
#endif
#ifndef HAVE_POP3
: - POP3 protocol
#endif
#ifndef HAVE_SMTP
: - SMTP protocol
#endif
#ifndef HAVE_SPAM
: - Interaction with spam filters
#endif
#ifndef HAVE_IDNA
: - IDNA (internationalized domain names for applications) support
#endif
#ifndef HAVE_LINE_EDITOR
: - Command line editing and history
#endif
#if 0
TODO disabled for v14.4, since not multibyte aware
ifndef HAVE_QUOTE_FOLD
: - Extended *quote-fold*ing
#endif
:
:Remarks:
#ifndef	HAVE_SNPRINTF
: . The function snprintf() could not be found. mailx will be compiled to use
: sprintf() instead. This might overflow buffers if input values are larger
: than expected. Use the resulting binary with care or update your system
: environment and start the configuration process again.
#endif
#ifndef	HAVE_FCHDIR
: . The function fchdir() could not be found. mailx will be compiled to use
: chdir() instead. This is not a problem unless the current working
: directory of mailx is moved while the IMAP cache is used.
#endif
#ifndef HAVE_GETOPT
: . A (usable) getopt() functionality could not be found.
: A builtin version is used instead.
#endif
#ifdef HAVE_ASSERTS
: . The binary will contain slow and huge debug code assertions.
: There are also additional commands available, like "core".
: Such a binary is not meant to be used by end-users, but only for
: development purposes.  Thanks!
#endif
:
!

${make} -f ${makefile} ${tmp2}.x
< ${tmp2}.x >&5 sed -e '/^[^:]/d; /^$/d; s/^://'

# vim:set fenc=utf-8:s-it-mode
