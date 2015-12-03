#!/bin/sh -
#@ Please see `INSTALL' and `make.rc' instead.

LC_ALL=C
export LC_ALL

option_reset() {
   WANT_ICONV=0
   WANT_SOCKETS=0
      WANT_SSL=0 WANT_ALL_SSL_ALGORITHMS=0
      WANT_SMTP=0 WANT_POP3=0
      WANT_GSSAPI=0 WANT_NETRC=0 WANT_AGENT=0
      #WANT_MD5=0
   WANT_IDNA=0
   WANT_IMAP_SEARCH=0
   WANT_REGEX=0
   WANT_READLINE=0 WANT_NCL=0
   WANT_TERMCAP=0
   WANT_ERRORS=0
   WANT_SPAM_SPAMC=0 WANT_SPAM_SPAMD=0 WANT_SPAM_FILTER=0
   WANT_DOCSTRINGS=0
   WANT_QUOTE_FOLD=0
   WANT_FILTER_HTML_TAGSOUP=0
   WANT_COLOUR=0
   WANT_DOTLOCK=0
}

option_maximal() {
   WANT_ICONV=require
   WANT_SOCKETS=1
      WANT_SSL=1 WANT_ALL_SSL_ALGORITHMS=1
      WANT_SMTP=1 WANT_POP3=1
      WANT_GSSAPI=1 WANT_NETRC=1 WANT_AGENT=1
      #WANT_MD5=1
   WANT_IDNA=1
   WANT_IMAP_SEARCH=1
   WANT_REGEX=require
   WANT_NCL=1
      WANT_HISTORY=1 WANT_TABEXPAND=1
   WANT_TERMCAP=1
   WANT_ERRORS=1
   WANT_SPAM_SPAMC=1 WANT_SPAM_SPAMD=1 WANT_SPAM_FILTER=1
   WANT_DOCSTRINGS=1
   WANT_QUOTE_FOLD=1
   WANT_FILTER_HTML_TAGSOUP=1
   WANT_COLOUR=1
   WANT_DOTLOCK=require
}

# Predefined CONFIG= urations take precedence over anything else
if [ -n "${CONFIG}" ]; then
   case "${CONFIG}" in
   [nN][uU][lL][lL])
      option_reset
      ;;
   [nN][uU][lL][lL][iI])
      option_reset
      WANT_ICONV=require
      ;;
   [mM][iI][nN][iI][mM][aA][lL])
      option_reset
      WANT_ICONV=1
      WANT_REGEX=1
      WANT_DOTLOCK=require
      ;;
   [mM][eE][dD][iI][uU][mM])
      option_reset
      WANT_ICONV=require
      WANT_IDNA=1
      WANT_REGEX=1
      WANT_NCL=1
         WANT_HISTORY=1
      WANT_ERRORS=1
      WANT_SPAM_FILTER=1
      WANT_DOCSTRINGS=1
      WANT_COLOUR=1
      WANT_DOTLOCK=require
      ;;
   [nN][eE][tT][sS][eE][nN][dD])
      option_reset
      WANT_ICONV=require
      WANT_SOCKETS=1
         WANT_SSL=require
         WANT_SMTP=require
         WANT_GSSAPI=1 WANT_NETRC=1 WANT_AGENT=1
      WANT_IDNA=1
      WANT_REGEX=1
      WANT_NCL=1
         WANT_HISTORY=1
      WANT_DOCSTRINGS=1
      WANT_COLOUR=1
      WANT_DOTLOCK=require
      ;;
   [mM][aA][xX][iI][mM][aA][lL])
      option_reset
      option_maximal
      ;;
   [dD][eE][vV][eE][lL])
      WANT_DEVEL=1 WANT_DEBUG=1 WANT_NYD2=1
      option_maximal
      ;;
   [oO][dD][eE][vV][eE][lL])
      WANT_DEVEL=1
      option_maximal
      ;;
   *)
      echo >&2 "Unknown CONFIG= setting: ${CONFIG}"
      echo >&2 'Possible values: NULL, NULLI, MINIMAL, MEDIUM, NETSEND, MAXIMAL'
      exit 1
      ;;
   esac
fi

# Inter-relationships
option_update() {
   if feat_no SMTP && feat_no POP3; then
      WANT_SOCKETS=0
   fi
   if feat_no SOCKETS; then
      if feat_require SMTP; then
         msg 'ERROR: need SOCKETS for required feature SMTP'
         config_exit 13
      fi
      if feat_require POP3; then
         msg 'ERROR: need SOCKETS for required feature POP3'
         config_exit 13
      fi
      WANT_SSL=0 WANT_ALL_SSL_ALGORITHMS=0
      WANT_SMTP=0 WANT_POP3=0
      WANT_GSSAPI=0 WANT_NETRC=0 WANT_AGENT=0
   fi
   if feat_no SMTP; then
      WANT_GSSAPI=0
   fi

   if feat_no READLINE && feat_no NCL; then
      WANT_HISTORY=0 WANT_TABEXPAND=0
   fi

   # If we don't need MD5 leave it alone
   if feat_no SOCKETS; then
      WANT_MD5=0
   fi

   if feat_yes DEVEL; then
      WANT_DEBUG=1
   fi
   if feat_yes DEBUG; then
      WANT_NOALLOCA=1 WANT_DEVEL=1
   fi
}

# Note that potential duplicates in PATH, C_INCLUDE_PATH etc. will be cleaned
# via path_check() later on once possible

# TODO cc_maxopt is brute simple, we should compile test program and dig real
# compiler versions for known compilers, then be more specific
cc_maxopt=100
_CFLAGS= _LDFLAGS=

os_early_setup() {
   i="${OS:-`uname -s`}"

   if [ ${i} = SunOS ]; then
      msg 'SunOS / Solaris?  Applying some "early setup" rules ...'
      _os_early_setup_sunos
   fi
}

os_setup() {
   # OSFULLSPEC is used to recognize changes (i.e., machine type, updates etc.)
   OSFULLSPEC="${OS:-`uname -a | ${tr} '[A-Z]' '[a-z]'`}"
   OS="${OS:-`uname -s | ${tr} '[A-Z]' '[a-z]'`}"
   msg 'Operating system is "%s"' ${OS}

   if [ ${OS} = sunos ]; then
      msg ' . have special SunOS / Solaris "setup" rules ...'
      _os_setup_sunos
   elif [ ${OS} = unixware ]; then
      msg ' . have special UnixWare environmental rules ...'
      if feat_yes AUTOCC && command -v cc >/dev/null 2>&1; then
         CC=cc
         feat_yes DEBUG && _CFLAGS='-v -Xa -g' || _CFLAGS='-Xa -O'

         CFLAGS="${_CFLAGS} ${ADDCFLAGS}"
         LDFLAGS="${_LDFLAGS} ${ADDLDFLAGS}"
         export CC CFLAGS LDFLAGS
         WANT_AUTOCC=0 had_want_autocc=1 need_R_ldflags=-R
      fi
   elif [ -n "${VERBOSE}" ]; then
      msg ' . no special treatment for this system necessary or known'
   fi

   # Sledgehammer: better set _GNU_SOURCE
   # And in general: oh, boy!
   OS_DEFINES="${OS_DEFINES}#define _GNU_SOURCE\n"
   #OS_DEFINES="${OS_DEFINES}#define _POSIX_C_SOURCE 200809L\n"
   #OS_DEFINES="${OS_DEFINES}#define _XOPEN_SOURCE 700\n"
   #[ ${OS} = darwin ] && OS_DEFINES="${OS_DEFINES}#define _DARWIN_C_SOURCE\n"

   # On pkgsrc(7) systems automatically add /usr/pkg/*
   if [ -d /usr/pkg ]; then
      C_INCLUDE_PATH="${C_INCLUDE_PATH}:/usr/pkg/include"
      LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:/usr/pkg/lib"
   fi
}

_os_early_setup_sunos() {
   # According to standards(5), this is what we need to do
   if [ -d /usr/xpg4 ]; then :; else
      msg 'ERROR: On SunOS / Solaris we need /usr/xpg4 environment!  Sorry.'
      config_exit 1
   fi
   PATH="/usr/xpg4/bin:/usr/ccs/bin:/usr/bin:${PATH}"
   [ -d /usr/xpg6 ] && PATH="/usr/xpg6/bin:${PATH}"
   export PATH
}

_os_setup_sunos() {
   C_INCLUDE_PATH="/usr/xpg4/include:${C_INCLUDE_PATH}"
   LD_LIBRARY_PATH="/usr/xpg4/lib:${LD_LIBRARY_PATH}"

   # Include packages
   if [ -d /opt/csw ]; then
      C_INCLUDE_PATH="${C_INCLUDE_PATH}:/opt/csw/include"
      LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:/opt/csw/lib"
   fi

   OS_DEFINES="${OS_DEFINES}#define __EXTENSIONS__\n"
   #OS_DEFINES="${OS_DEFINES}#define _POSIX_C_SOURCE 200112L\n"

   [ -n "${cksum}" ] || cksum=/opt/csw/gnu/cksum
   if [ -x "${cksum}" ]; then :; else
      msg 'ERROR: Not an executable program: "%s"' "${cksum}"
      msg 'ERROR:   We need a CRC-32 cksum(1), as specified in POSIX.'
      msg 'ERROR:   However, we do so only for tests.'
      msg 'ERROR:   If that is ok, set "cksum=/usr/bin/true", then rerun'
      config_exit 1
   fi

   if feat_yes AUTOCC; then
      if command -v cc >/dev/null 2>&1; then
         CC=cc
         feat_yes DEBUG && _CFLAGS="-v -Xa -g" || _CFLAGS="-Xa -O"

         CFLAGS="${_CFLAGS} ${ADDCFLAGS}"
         LDFLAGS="${_LDFLAGS} ${ADDLDFLAGS}"
         export CC CFLAGS LDFLAGS
         WANT_AUTOCC=0 had_want_autocc=1 need_R_ldflags=-R
      else
         # Assume gcc(1)
         cc_maxopt=2 force_no_stackprot=1 need_R_ldflags=-Wl,-R
      fi
   fi
}

# Check out compiler ($CC) and -flags ($CFLAGS)
cc_setup() {
   # Even though it belongs into cc_flags we will try to compile and link
   # something, so ensure we have a clean state regarding CFLAGS/LDFLAGS or
   # ADDCFLAGS/ADDLDFLAGS
   if feat_no AUTOCC; then
      _cc_default
      # Ensure those don't do any harm
      ADDCFLAGS= ADDLDFLAGS=
      export ADDCFLAGS ADDLDFLAGS
      return
   else
      CFLAGS= LDFLAGS=
      export CFLAGS LDFLAGS
   fi

   [ -n "${CC}" ] && [ "${CC}" != cc ] && { _cc_default; return; }

   printf >&2 'Searching for a usable C compiler .. $CC='
   if { i="`command -v clang`"; }; then
      CC=${i}
   elif { i="`command -v gcc`"; }; then
      CC=${i}
   elif { i="`command -v c99`"; }; then
      CC=${i}
   elif { i="`command -v tcc`"; }; then
      CC=${i}
   elif { i="`command -v pcc`"; }; then
      CC=${i}
   else
      if [ "${CC}" = cc ]; then
         :
      elif { i="`command -v c89`"; }; then
         CC=${i}
      else
         printf >&2 'boing booom tschak\n'
         msg 'ERROR: I cannot find a compiler!'
         msg ' Neither of clang(1), gcc(1), tcc(1), c89(1) and c99(1).'
         msg ' Please set $CC environment variable, maybe $CFLAGS also, rerun.'
         config_exit 1
      fi
   fi
   printf >&2 -- '"%s"\n' "${CC}"
   export CC
}

_cc_default() {
   if [ -z "${CC}" ]; then
      printf >&2 'To go on like you have chosen, please set $CC, rerun.'
      config_exit 1
   fi

   if [ -z "${VERBOSE}" ] && [ -f ${lst} ] && feat_no DEBUG; then
      :
   else
      msg 'Using C compiler $CC="%s"' "${CC}"
   fi
}

cc_flags() {
   if feat_yes AUTOCC; then
      if [ -f ${lst} ] && feat_no DEBUG && [ -z "${VERBOSE}" ]; then
         cc_check_silent=1
         msg 'Detecting $CFLAGS/$LDFLAGS for $CC="%s", just a second..' "${CC}"
      else
         cc_check_silent=
         msg 'Testing usable $CFLAGS/$LDFLAGS for $CC="%s"' "${CC}"
      fi

      i=`echo "${CC}" | ${awk} 'BEGIN{FS="/"}{print $NF}'`
      if { echo "${i}" | ${grep} tcc; } >/dev/null 2>&1; then
         msg ' . have special tcc(1) environmental rules ...'
         _cc_flags_tcc
      else
         # As of pcc CVS 2016-04-02, stack protection support is announced but
         # will break if used on Linux
         if { echo "${i}" | ${grep} pcc; } >/dev/null 2>&1; then
            force_no_stackprot=1
         fi
         _cc_flags_generic
      fi

      feat_no DEBUG && _CFLAGS="-DNDEBUG ${_CFLAGS}"
      CFLAGS="${_CFLAGS} ${ADDCFLAGS}"
      LDFLAGS="${_LDFLAGS} ${ADDLDFLAGS}"
   else
      if feat_no DEBUG; then
         CFLAGS="-DNDEBUG ${CFLAGS}"
      fi
   fi
   msg ''
   export CFLAGS LDFLAGS
}

_cc_flags_tcc() {
   __cflags=${_CFLAGS} __ldflags=${_LDFLAGS}
   _CFLAGS= _LDFLAGS=

   cc_check -Wall
   cc_check -Wextra
   cc_check -pedantic

   if feat_yes DEBUG; then
      # May have problems to find libtcc cc_check -b
      cc_check -g
   fi

   _CFLAGS="${_CFLAGS} ${__cflags}" _LDFLAGS="${_LDFLAGS} ${__ldflags}"
   unset __cflags __ldflags
}

_cc_flags_generic() {
   __cflags=${_CFLAGS} __ldflags=${_LDFLAGS}
   _CFLAGS= _LDFLAGS=
   feat_yes DEVEL && cc_check -std=c89 || cc_check -std=c99

   # Check -g first since some others may rely upon -g / optim. level
   if feat_yes DEBUG; then
      cc_check -O
      cc_check -g
   elif [ ${cc_maxopt} -gt 2 ] && cc_check -O3; then
      :
   elif [ ${cc_maxopt} -gt 1 ] && cc_check -O2; then
      :
   elif [ ${cc_maxopt} -gt 0 ] && cc_check -O1; then
      :
   else
      cc_check -O
   fi

   if feat_yes DEVEL && cc_check -Weverything; then
      :
   else
      cc_check -Wall
      cc_check -Wextra
      cc_check -Wbad-function-cast
      cc_check -Wcast-align
      cc_check -Wcast-qual
      cc_check -Winit-self
      cc_check -Wmissing-prototypes
      cc_check -Wshadow
      cc_check -Wunused
      cc_check -Wwrite-strings
      cc_check -Wno-long-long
   fi
   cc_check -pedantic

   if feat_yes AMALGAMATION && feat_no DEVEL; then
      cc_check -Wno-unused-function
   fi
   feat_no DEVEL && cc_check -Wno-unused-result # XXX do right way (pragma too)

   cc_check -fno-unwind-tables
   cc_check -fno-asynchronous-unwind-tables
   cc_check -fstrict-aliasing
   if cc_check -fstrict-overflow && feat_yes DEVEL; then
      cc_check -Wstrict-overflow=5
   fi

   if feat_yes DEBUG || feat_yes FORCED_STACKPROT; then
      if [ -z "${force_no_stackprot}" ]; then
         if cc_check -fstack-protector-strong ||
               cc_check -fstack-protector-all; then
            cc_check -D_FORTIFY_SOURCE=2
         fi
      else
         msg 'Not checking for -fstack-protector compiler option,'
         msg 'since that caused errors in a "similar" configuration.'
         msg 'You may turn off WANT_AUTOCC and use your own settings, rerun'
      fi
   fi

   if feat_yes AMALGAMATION; then
      cc_check -pipe
   fi

   # LD (+ dependend CC)

   if feat_yes DEVEL; then
      _ccfg=${_CFLAGS}
      # -fsanitize=address
      #if cc_check -fsanitize=memory &&
      #      ld_check -fsanitize=memory &&
      #      cc_check -fsanitize-memory-track-origins=2 &&
      #      ld_check -fsanitize-memory-track-origins=2; then
      #   :
      #else
      #   _CFLAGS=${_ccfg}
      #fi
   fi

   ld_check -Wl,-z,relro
   ld_check -Wl,-z,now
   ld_check -Wl,-z,noexecstack

   # Address randomization
   _ccfg=${_CFLAGS}
   if cc_check -fPIE || cc_check -fpie; then
      ld_check -pie || _CFLAGS=${_ccfg}
   fi
   unset _ccfg

   _CFLAGS="${_CFLAGS} ${__cflags}" _LDFLAGS="${_LDFLAGS} ${__ldflags}"
   unset __cflags __ldflags
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
   printf >&2 -- "${fmt}\\n" "${@}"
}

## First of all, create new configuration and check wether it changed

rc=./make.rc
lst=./config.lst
h=./config.h h_name=config.h
mk=./mk.mk

newlst=./config.lst-new
newmk=./config.mk-new
newh=./config.h-new
tmp0=___tmp
tmp=./${tmp0}1$$
tmp2=./${tmp0}2$$

t1=ten10one1ten10one1
if ( [ ${t1##*ten10} = one1 ] && [ ${t1#*ten10} = one1ten10one1 ] &&
      [ ${t1%%one1*} = ten10 ] && [ ${t1%one1*} = ten10one1ten10 ]
      ) > /dev/null 2>&1; then
   good_shell=1
else
   unset good_shell
fi
unset t1

# We need some standard utilities
unset -f command
check_tool() {
   n=${1} i=${2} opt=${3:-0}
   # Evaluate, just in case user comes in with shell snippets (..well..)
   eval i="${i}"
   if type "${i}" >/dev/null 2>&1; then # XXX why have i type not command -v?
      [ -n "${VERBOSE}" ] && msg ' . $%s ... "%s"' "${n}" "${i}"
      eval ${n}=${i}
      return 0
   fi
   if [ ${opt} -eq 0 ]; then
      msg 'ERROR: no trace of utility "%s"' "${n}"
      config_exit 1
   fi
   return 1
}

# Very easy checks for the operating system in order to be able to adjust paths
# or similar very basic things which we need to be able to go at all
os_early_setup

# Check those tools right now that we need before including $rc
msg 'Checking for basic utility set'
check_tool awk "${awk:-`command -v awk`}"
check_tool rm "${rm:-`command -v rm`}"
check_tool tr "${tr:-`command -v tr`}"

# Our feature check environment
feat_val_no() {
   [ "x${1}" = x0 ] ||
   [ "x${1}" = xfalse ] || [ "x${1}" = xno ] || [ "x${1}" = xoff ]
}

feat_val_yes() {
   [ "x${1}" = x1 ] ||
   [ "x${1}" = xtrue ] || [ "x${1}" = xyes ] || [ "x${1}" = xon ] ||
         [ "x${1}" = xrequire ]
}

feat_val_require() {
   [ "x${1}" = xrequire ]
}

_feat_check() {
   eval i=\$WANT_${1}
   i="`echo ${i} | ${tr} '[A-Z]' '[a-z]'`"
   if feat_val_no "${i}"; then
      return 1
   elif feat_val_yes "${i}"; then
      return 0
   else
      msg "ERROR: %s: any of 0/false/no/off or 1/true/yes/on/require, got: %s" \
         "${1}" "${i}"
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
   i="`echo ${i} | ${tr} '[A-Z]' '[a-z]'`"
   [ "x${i}" = xrequire ] || [ "x${i}" = xrequired ]
}

feat_bail_required() {
   if feat_require ${1}; then
      msg 'ERROR: feature WANT_%s is required but not available' "${1}"
      config_exit 13
   fi
   eval WANT_${1}=0
   option_update # XXX this is rather useless here (dependency chain..)
}

# Include $rc, but only take from it what wasn't overwritten by the user from
# within the command line or from a chosen fixed CONFIG=
# Note we leave alone the values
trap "exit 1" HUP INT TERM
trap "${rm} -f ${tmp}" EXIT

printf >&2 'Reading and preparing configuration from "%s" ... ' ${rc}
${rm} -f ${tmp}
# We want read(1) to perform backslash escaping in order to be able to use
# multiline values in make.rc; the resulting sh(1)/sed(1) code was very slow in
# VMs (see [fa2e248]), Aharon Robbins suggested the following
< ${rc} ${awk} 'BEGIN{line = ""}{
   gsub(/^[[:space:]]+/, "", $0)
   gsub(/[[:space:]]+$/, "", $0)
   if(gsub(/\\$/, "", $0)){
      line = line $0
      next
   }else
      line = line $0
   if(index(line, "#") == 1){
      line = ""
   }else if(length(line)){
      print line
      line = ""
   }
}' |
while read line; do
   if [ -n "${good_shell}" ]; then
      i=${line%%=*}
   else
      i=`${awk} -v LINE="${line}" 'BEGIN{
         gsub(/=.*$/, "", LINE)
         print LINE
      }'`
   fi
   if [ "${i}" = "${line}" ]; then
      msg 'ERROR: invalid syntax in "%s"' "${line}"
      continue
   fi

   eval j="\$${i}" jx="\${${i}+x}"
   if [ -n "${j}" ] || [ "${jx}" = x ]; then
      : # Yet present
   else
      j=`${awk} -v LINE="${line}" 'BEGIN{
         gsub(/^[^=]*=/, "", LINE)
         gsub(/^\"*/, "", LINE)
         gsub(/\"*$/, "", LINE)
         print LINE
      }'`
   fi
  echo "${i}=\"${j}\""
done > ${tmp}
# Reread the mixed version right now
. ./${tmp}
printf >&2 'done\n'

# We need to know about that now, in order to provide utility overwrites etc.
os_setup

msg 'Checking for remaining set of utilities'
check_tool grep "${grep:-`command -v grep`}"

# Before we step ahead with the other utilities perform a path cleanup first.
# We need this function also for C_INCLUDE_PATH and LD_LIBRARY_PATH
# "path_check VARNAME" or "path_check VARNAME FLAG VARNAME"
path_check() {
   varname=${1} addflag=${2} flagvarname=${3}
   j=${IFS}
   IFS=:
   eval "set -- \$${1}"
   IFS=${j}
   j= k= y= z=
   for i
   do
      [ -z "${i}" ] && continue
      [ -d "${i}" ] || continue
      if [ -n "${j}" ]; then
         if { z=${y}; echo "${z}"; } | ${grep} ":${i}:" >/dev/null 2>&1; then
            :
         else
            y="${y} :${i}:"
            j="${j}:${i}"
            [ -n "${addflag}" ] && k="${k} ${addflag}${i}"
         fi
      else
         y=" :${i}:"
         j="${i}"
         [ -n "${addflag}" ] && k="${addflag}${i}"
      fi
   done
   eval "${varname}=\"${j}\""
   [ -n "${addflag}" ] && eval "${flagvarname}=\"${k}\""
   unset varname
}

path_check PATH

# awk(1) above
check_tool cat "${cat:-`command -v cat`}"
check_tool chmod "${chmod:-`command -v chmod`}"
check_tool cp "${cp:-`command -v cp`}"
check_tool cmp "${cmp:-`command -v cmp`}"
# grep(1) above
check_tool mkdir "${mkdir:-`command -v mkdir`}"
check_tool mv "${mv:-`command -v mv`}"
# rm(1) above
check_tool sed "${sed:-`command -v sed`}"
check_tool sort "${sort:-`command -v sort`}"
check_tool tee "${tee:-`command -v tee`}"

check_tool chown "${chown:-`command -v chown`}" 1 ||
   check_tool chown "/sbin/chown" 1 ||
   check_tool chown "/usr/sbin/chown"

check_tool make "${MAKE:-`command -v make`}"
MAKE=${make}
check_tool strip "${STRIP:-`command -v strip`}" 1 &&
   HAVE_STRIP=1 || HAVE_STRIP=0

# For ./cc-test.sh only
check_tool cksum "${cksum:-`command -v cksum`}"

# Update WANT_ options now, in order to get possible inter-dependencies right
option_update

# (No functions since some shells loose non-exported variables in traps)
trap "trap \"\" HUP INT TERM; exit 1" HUP INT TERM
trap "trap \"\" HUP INT TERM EXIT;\
   ${rm} -rf ${newlst} ${tmp0}.* ${tmp0}* ${newmk} ${newh}" EXIT

# Our configuration options may at this point still contain shell snippets,
# we need to evaluate them in order to get them expanded, and we need those
# evaluated values not only in our new configuration file, but also at hand..
printf >&2 'Evaluating all configuration items ... '
${rm} -f ${newlst} ${newmk} ${newh}
exec 5<&0 6>&1 <${tmp} >${newlst}
while read line; do
   z=
   if [ -n "${good_shell}" ]; then
      i=${line%%=*}
      [ "${i}" != "${i#WANT_}" ] && z=1
   else
      i=`${awk} -v LINE="${line}" 'BEGIN{
         gsub(/=.*$/, "", LINE);\
         print LINE
      }'`
      if echo "${i}" | ${grep} '^WANT_' >/dev/null 2>&1; then
         z=1
      fi
   fi

   eval j=\$${i}
   if [ -n "${z}" ]; then
      j="`echo ${j} | ${tr} '[A-Z]' '[a-z]'`"
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
         msg 'ERROR: cannot parse <%s>' "${line}"
         config_exit 1
      fi
   else
      printf "#define ${i} \"${j}\"\n" >> ${newh}
   fi
   printf "${i} = ${j}\n" >> ${newmk}
   printf "${i}=${j}\n"
   eval "${i}=\"${j}\""
done
exec 0<&5 1>&6 5<&- 6<&-
printf >&2 'done\n'

# Add the known utility and some other variables
printf "#define UAGENT \"${SID}${NAIL}\"\n" >> ${newh}
printf "UAGENT = ${SID}${NAIL}\n" >> ${newmk}

printf "#define PRIVSEP \"${SID}${NAIL}-privsep\"\n" >> ${newh}
printf "PRIVSEP = \$(UAGENT)-privsep\n" >> ${newmk}
if feat_yes DOTLOCK; then
   printf "OPTIONAL_PRIVSEP = \$(PRIVSEP)\n" >> ${newmk}
else
   printf "OPTIONAL_PRIVSEP =\n" >> ${newmk}
fi

for i in \
      awk cat chmod chown cp cmp grep mkdir mv rm sed sort tee tr \
      MAKE make strip \
      cksum; do
   eval j=\$${i}
   printf "${i} = ${j}\n" >> ${newmk}
   printf "${i}=${j}\n" >> ${newlst}
done

# Build a basic set of INCS and LIBS according to user environment.
path_check C_INCLUDE_PATH -I _INCS
INCS="${INCS} ${_INCS}"
path_check LD_LIBRARY_PATH -L _LIBS
LIBS="${LIBS} ${_LIBS}"
unset _INCS _LIBS
export C_INCLUDE_PATH LD_LIBRARY_PATH

if [ -n "${need_R_ldflags}" ]; then
   i=${IFS}
   IFS=:
   set -- ${LD_LIBRARY_PATH}
   IFS=${i}
   for i
   do
      LDFLAGS="${LDFLAGS} ${need_R_ldflags}${i}"
      _LDFLAGS="${_LDFLAGS} ${need_R_ldflags}${i}"
   done
   export LDFLAGS
fi

## Detect CC, wether we can use it, and possibly which CFLAGS we can use

cc_setup

${cat} > ${tmp}.c << \!
#include <stdio.h>
#include <string.h>
static void doit(char const *s);
int
main(int argc, char **argv){
   (void)argc;
   (void)argv;
   doit("Hello world");
   return 0;
}
static void
doit(char const *s){
   char buf[12];
   strcpy(buf, s);
   puts(s);
}
!

if "${CC}" ${INCS} ${CFLAGS} ${ADDCFLAGS} ${LDFLAGS} ${ADDLDFLAGS} \
      -o ${tmp2} ${tmp}.c ${LIBS}; then
   :
else
   msg 'ERROR: i cannot compile a "Hello world" via'
   msg '   %s' \
      "${CC} ${INCS} ${CFLAGS} ${ADDCFLAGS} ${LDFLAGS} ${ADDLDFLAGS} ${LIBS}"
   msg 'ERROR:   Please read INSTALL, rerun'
   config_exit 1
fi

cc_check() {
   [ -n "${cc_check_silent}" ] || printf >&2 ' . CC %s .. ' "${1}"
   if "${CC}" ${INCS} ${_CFLAGS} ${1} ${ADDCFLAGS} ${_LDFLAGS} ${ADDLDFLAGS} \
         -o ${tmp2} ${tmp}.c ${LIBS} >/dev/null 2>&1; then
      _CFLAGS="${_CFLAGS} ${1}"
      [ -n "${cc_check_silent}" ] || printf >&2 'yes\n'
      return 0
   fi
   [ -n "${cc_check_silent}" ] || printf >&2 'no\n'
   return 1
}

ld_check() {
   [ -n "${cc_check_silent}" ] || printf >&2 ' . LD %s .. ' "${1}"
   if "${CC}" ${INCS} ${_CFLAGS} ${_LDFLAGS} ${1} ${ADDLDFLAGS} \
         -o ${tmp2} ${tmp}.c ${LIBS} >/dev/null 2>&1; then
      _LDFLAGS="${_LDFLAGS} ${1}"
      [ -n "${cc_check_silent}" ] || printf >&2 'yes\n'
      return 0
   fi
   [ -n "${cc_check_silent}" ] || printf >&2 'no\n'
   return 1
}

cc_flags

for i in \
      INCS LIBS \
      ; do
   eval j=\$${i}
   printf -- "${i}=${j}\n" >> ${newlst}
done
for i in \
      CC \
      CFLAGS \
      LDFLAGS \
      PATH C_INCLUDE_PATH LD_LIBRARY_PATH \
      OSFULLSPEC \
      ; do
   eval j=\$${i}
   printf -- "${i} = ${j}\n" >> ${newmk}
   printf -- "${i}=${j}\n" >> ${newlst}
done

# Now finally check wether we already have a configuration and if so, wether
# all those parameters are still the same.. or something has actually changed
if [ -f ${lst} ] && ${cmp} ${newlst} ${lst} >/dev/null 2>&1; then
   echo 'Configuration is up-to-date'
   exit 0
elif [ -f ${lst} ]; then
   echo 'Configuration has been updated..'
   ( eval "${MAKE} -f ./mk.mk clean" )
   echo
else
   echo 'Shiny configuration..'
fi

# Time to redefine helper 1
config_exit() {
   ${rm} -f ${lst} ${h} ${mk}
   exit ${1}
}

${mv} -f ${newlst} ${lst}
${mv} -f ${newh} ${h}
${mv} -f ${newmk} ${mk}

## Compile and link checking

tmp3=./${tmp0}3$$
log=./config.log
lib=./config.lib
inc=./config.inc
makefile=./config.mk

# (No function since some shells loose non-exported variables in traps)
trap "trap \"\" HUP INT TERM;\
   ${rm} -f ${lst} ${h} ${mk} ${lib} ${inc}; exit 1" HUP INT TERM
trap "trap \"\" HUP INT TERM EXIT;\
   ${rm} -rf ${tmp0}.* ${tmp0}* ${makefile}" EXIT

# Time to redefine helper 2
msg() {
   fmt=${1}
   shift
   printf "*** ${fmt}\\n" "${@}"
   printf -- "${fmt}\\n" "${@}" >&5
}
msg_nonl() {
   fmt=${1}
   shift
   printf "*** ${fmt}\\n" "${@}"
   printf -- "${fmt}" "${@}" >&5
}

exec 5>&2 > ${log} 2>&1

echo "${LIBS}" > ${lib}
echo "${INCS}" > ${inc}
${cat} > ${makefile} << \!
.SUFFIXES: .o .c .x .y
.c.o:
	$(CC) -I./ $(XINCS) $(CFLAGS) -c $<
.c.x:
	$(CC) -I./ $(XINCS) -E $< >$@
.c:
	$(CC) -I./ $(XINCS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(XLIBS)
.y: ;
!

_check_preface() {
   variable=$1 topic=$2 define=$3

   echo '**********'
   msg_nonl ' . %s ... ' "${topic}"
   echo "/* checked ${topic} */" >> ${h}
   ${rm} -f ${tmp} ${tmp}.o
   echo '*** test program is'
   { echo '#include <'"${h_name}"'>'; cat; } | ${tee} ${tmp}.c
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
      msg 'yes'
      echo "${define}" >> ${h}
      eval have_${variable}=yes
      return 0
   else
      echo "/* ${define} */" >> ${h}
      msg 'no'
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
      echo "*** adding INCS<${incs}> LIBS<${libs}>; executed: ${run}"
      msg 'yes'
      echo "${define}" >> ${h}
      LIBS="${LIBS} ${libs}"
      echo "${libs}" >> ${lib}
      INCS="${INCS} ${incs}"
      echo "${incs}" >> ${inc}
      eval have_${variable}=yes
      return 0
   else
      msg 'no'
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

##

# May be multiline..
[ -n "${OS_DEFINES}" ] && printf -- "${OS_DEFINES}" >> ${h}

if run_check inline '"inline" functions' \
   '#define HAVE_INLINE
   #define n_INLINE static inline' << \!
static inline int ilf(int i){return ++i;}
int main(void){return ilf(-1);}
!
then
   :
elif run_check inline '"__inline" functions' \
   '#define HAVE_INLINE
   #define n_INLINE static __inline' << \!
static __inline int ilf(int i){return ++i;}
int main(void){return ilf(-1);}
!
then
   :
fi

if run_check endian 'Little endian byteorder' \
   '#define HAVE_BYTE_ORDER_LITTLE' << \!
int main(void){
   enum {vBig = 1, vLittle = 0};
   union {unsigned short bom; unsigned char buf[2];} u;
   u.bom = 0xFEFF;
   return((u.buf[1] == 0xFE) ? vLittle : vBig);
}
!
then
   :
fi

##

if run_check clock_gettime 'clock_gettime(2)' \
   '#define HAVE_CLOCK_GETTIME' << \!
#include <time.h>
# include <errno.h>
int main(void){
   struct timespec ts;

   if(!clock_gettime(CLOCK_REALTIME, &ts) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
elif run_check clock_gettime 'clock_gettime(2) (via -lrt)' \
   '#define HAVE_CLOCK_GETTIME' '-lrt' << \!
#include <time.h>
# include <errno.h>
int main(void){
   struct timespec ts;

   if(!clock_gettime(CLOCK_REALTIME, &ts) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
elif run_check gettimeofday 'gettimeofday(2)' \
   '#define HAVE_GETTIMEOFDAY' << \!
#include <stdio.h> /* For C89 NULL */
#include <sys/time.h>
# include <errno.h>
int main(void){
   struct timeval tv;

   if(!gettimeofday(&tv, NULL) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
else
   have_no_subsecond_time=1
fi

if run_check nanosleep 'nanosleep(2)' \
   '#define HAVE_NANOSLEEP' << \!
#include <time.h>
# include <errno.h>
int main(void){
   struct timespec ts;

   ts.tv_sec = 1;
   ts.tv_nsec = 100000;
   if(!nanosleep(&ts, NULL) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
elif run_check nanosleep 'nanosleep(2) (via -lrt)' \
   '#define HAVE_NANOSLEEP' '-lrt' << \!
#include <time.h>
# include <errno.h>
int main(void){
   struct timespec ts;

   ts.tv_sec = 1;
   ts.tv_nsec = 100000;
   if(!nanosleep(&ts, NULL) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
# link_check is enough for this, that function is so old, trust the proto
elif link_check sleep 'sleep(3)' \
   '#define HAVE_SLEEP' << \!
#include <unistd.h>
# include <errno.h>
int main(void){
   if(!sleep(1) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
else
   msg 'ERROR: we require one of nanosleep(2) and sleep(3).'
   config_exit 1
fi

if run_check userdb 'gete?[gu]id(2), getpwuid(3), getpwnam(3)' << \!
#include <pwd.h>
#include <unistd.h>
# include <errno.h>
int main(void){
   struct passwd *pw;
   gid_t gid;
   uid_t uid;

   if((gid = getgid()) != 0)
      gid = getegid();
   if((uid = getuid()) != 0)
      uid = geteuid();
   if((pw = getpwuid(uid)) == NULL && errno == ENOSYS)
      return 1;
   if((pw = getpwnam("root")) == NULL && errno == ENOSYS)
      return 1;
   return 0;
}
!
then
   :
else
   msg 'ERROR: we require user and group info / database searches.'
   msg 'That much Unix we indulge ourselfs.'
   config_exit 1
fi

if link_check snprintf 'v?snprintf(3)' << \!
#include <stdarg.h>
#include <stdio.h>
static void dome(char *buf, ...){
   va_list ap;

   va_start(ap, buf);
   vsnprintf(buf, 20, "%s", ap);
   va_end(ap);
   return;
}
int main(void){
   char b[20];

   snprintf(b, sizeof b, "%s", "string");
   dome(b, "string");
   return 0;
}
!
then
   :
else
   msg 'ERROR: we require the snprintf(3) and vsnprintf(3) functions.'
   config_exit 1
fi

if link_check environ 'environ(3)' << \!
#include <stdio.h> /* For C89 NULL */
int main(void){
   extern char **environ;

   return environ[0] == NULL;
}
!
then
   :
else
   msg 'ERROR: we require the environ(3) array for subprocess control.'
   config_exit 1
fi

if link_check termios 'termios.h and tc*(3) family' << \!
#include <termios.h>
int main(void){
   struct termios tios;

   tcgetattr(0, &tios);
   tcsetattr(0, TCSADRAIN | TCSAFLUSH, &tios);
   return 0;
}
!
then
   :
else
   msg 'ERROR: we require termios.h and the tc*() family of functions.'
   msg 'That much Unix we indulge ourselfs.'
   config_exit 1
fi

##

run_check pathconf 'pathconf(2)' '#define HAVE_PATHCONF' << \!
#include <unistd.h>
#include <errno.h>
int main(void){
   int rv = 0;

   errno = 0;
   rv |= !(pathconf(".", _PC_NAME_MAX) >= 0 || errno == 0 || errno != ENOSYS);
   errno = 0;
   rv |= !(pathconf(".", _PC_PATH_MAX) >= 0 || errno == 0 || errno != ENOSYS);
   return rv;
}
!

run_check pipe2 'pipe2(2)' '#define HAVE_PIPE2' << \!
#include <fcntl.h>
#include <unistd.h>
# include <errno.h>
int main(void){
   int fds[2];

   if(!pipe2(fds, O_CLOEXEC) || errno != ENOSYS)
      return 0;
   return 1;
}
!

# We use this only then for now (need NOW+1)
run_check utimensat 'utimensat(2)' '#define HAVE_UTIMENSAT' << \!
#include <fcntl.h> /* For AT_* */
#include <sys/stat.h>
# include <errno.h>
int main(void){
   struct timespec ts[2];

   ts[0].tv_nsec = UTIME_NOW;
   ts[1].tv_nsec = UTIME_OMIT;
   if(!utimensat(AT_FDCWD, "", ts, 0) || errno != ENOSYS)
      return 0;
   return 1;
}
!

##

# XXX Add POSIX check once standardized
if link_check posix_random 'arc4random(3)' '#define HAVE_POSIX_RANDOM 0' << \!
#include <stdlib.h>
int main(void){
   arc4random();
   return 0;
}
!
then
   :
elif [ -n "${have_no_subsecond_time}" ]; then
   msg 'ERROR: %s %s' 'without a native random' \
      'one of clock_gettime(2) and gettimeofday(2) is required.'
   config_exit 1
fi

link_check setenv 'setenv(3)/unsetenv(3)' '#define HAVE_SETENV' << \!
#include <stdlib.h>
int main(void){
   setenv("s-nail", "to be made nifty!", 1);
   unsetenv("s-nail");
   return 0;
}
!

link_check putc_unlocked 'putc_unlocked(3)' '#define HAVE_PUTC_UNLOCKED' <<\!
#include <stdio.h>
int main(void){
   putc_unlocked('@', stdout);
   return 0;
}
!

link_check fchdir 'fchdir(3)' '#define HAVE_FCHDIR' << \!
#include <unistd.h>
int main(void){
   fchdir(0);
   return 0;
}
!

link_check setlocale 'setlocale(3)' '#define HAVE_SETLOCALE' << \!
#include <locale.h>
int main(void){
   setlocale(LC_ALL, "");
   return 0;
}
!

if [ "${have_setlocale}" = yes ]; then
   link_check c90amend1 'ISO/IEC 9899:1990/Amendment 1:1995' \
      '#define HAVE_C90AMEND1' << \!
#include <limits.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
int main(void){
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
      link_check wcwidth 'wcwidth(3)' '#define HAVE_WCWIDTH' << \!
#include <wchar.h>
int main(void){
   wcwidth(L'c');
   return 0;
}
!
   fi

   link_check nl_langinfo 'nl_langinfo(3)' '#define HAVE_NL_LANGINFO' << \!
#include <langinfo.h>
#include <stdlib.h>
int main(void){
   nl_langinfo(DAY_1);
   return (nl_langinfo(CODESET) == NULL);
}
!
fi # have_setlocale

run_check realpath 'realpath(3)' '#define HAVE_REALPATH' << \!
#include <stdlib.h>
int main(void){
#if 1 /* TODO for now we use realpath(3) without NULL as 2nd arg! */
   /* (And note that on Linux tcc(1) otherwise didn't detect once tested! */
   char x_buf[4096], *x = realpath(".", x_buf);

   return (x != NULL) ? 0 : 1;
#else
   char *x = realpath(".", NULL), *y = realpath("/", NULL);

   return (x != NULL && y != NULL) ? 0 : 1;
#endif
}
!

link_check wordexp 'wordexp(3)' '#define HAVE_WORDEXP' << \!
#include <stdio.h> /* For C89 NULL */
#include <wordexp.h>
int main(void){
   wordexp(NULL, NULL, 0);
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
   run_check alloca '__builtin_alloca()' \
      '#define HAVE_ALLOCA __builtin_alloca' << \!
#include <stdio.h> /* For C89 NULL */
int main(void){
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

if feat_yes DOTLOCK; then
   if run_check readlink 'readlink(2)' << \!
#include <unistd.h>
# include <errno.h>
int main(void){
   char buf[128];

   if(!readlink("here", buf, sizeof buf) || errno != ENOSYS)
      return 0;
   return 1;
}
!
   then
      :
   else
      feat_bail_required DOTLOCK
   fi
fi

if feat_yes DOTLOCK; then
   if run_check fchown 'fchown(2)' << \!
#include <unistd.h>
# include <errno.h>
int main(void){
   if(!fchown(0, 0, 0) || errno != ENOSYS)
      return 0;
   return 1;
}
!
   then
      :
   else
      feat_bail_required DOTLOCK
   fi
fi

##

if feat_yes ICONV; then
   ${cat} > ${tmp2}.c << \!
#include <stdio.h> /* For C89 NULL */
#include <iconv.h>
int main(void){
   iconv_t id;

   id = iconv_open("foo", "bar");
   iconv(id, NULL, NULL, NULL, NULL);
   iconv_close(id);
   return 0;
}
!
   < ${tmp2}.c link_check iconv 'iconv(3) functionality' \
         '#define HAVE_ICONV' ||
      < ${tmp2}.c link_check iconv 'iconv(3) functionality (via -liconv)' \
         '#define HAVE_ICONV' '-liconv' ||
      feat_bail_required ICONV
else
   echo '/* WANT_ICONV=0 */' >> ${h}
fi # feat_yes ICONV

if feat_yes SOCKETS || feat_yes SPAM_SPAMD; then
   ${cat} > ${tmp2}.c << \!
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
# include <errno.h>
int main(void){
   struct sockaddr_un soun;

   if(socket(AF_UNIX, SOCK_STREAM, 0) == -1 && errno == ENOSYS)
      return 1;
   if(connect(0, (struct sockaddr*)&soun, 0) == -1 && errno == ENOSYS)
      return 1;
   if(shutdown(0, SHUT_RD | SHUT_WR | SHUT_RDWR) == -1 && errno == ENOSYS)
      return 1;
   return 0;
}
!

   < ${tmp2}.c run_check af_unix 'AF_UNIX sockets' \
         '#define HAVE_UNIX_SOCKETS' ||
      < ${tmp2}.c run_check af_unix 'AF_UNIX sockets (via -lnsl)' \
         '#define HAVE_UNIX_SOCKETS' '-lnsl' ||
      < ${tmp2}.c run_check af_unix 'AF_UNIX sockets (via -lsocket -lnsl)' \
         '#define HAVE_UNIX_SOCKETS' '-lsocket -lnsl'
fi

if feat_yes SOCKETS; then
   ${cat} > ${tmp2}.c << \!
#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
# include <errno.h>
int main(void){
   struct sockaddr s;

   if(socket(AF_INET, SOCK_STREAM, 0) == -1 && errno == ENOSYS)
      return 1;
   if(connect(0, &s, 0) == -1 && errno == ENOSYS)
      return 1;
   return 0;
}
!

   < ${tmp2}.c run_check sockets 'sockets' \
         '#define HAVE_SOCKETS' ||
      < ${tmp2}.c run_check sockets 'sockets (via -lnsl)' \
         '#define HAVE_SOCKETS' '-lnsl' ||
      < ${tmp2}.c run_check sockets 'sockets (via -lsocket -lnsl)' \
         '#define HAVE_SOCKETS' '-lsocket -lnsl' ||
      feat_bail_required SOCKETS
else
   echo '/* WANT_SOCKETS=0 */' >> ${h}
fi # feat_yes SOCKETS

if feat_yes SOCKETS; then
   link_check getaddrinfo 'getaddrinfo(3)' \
      '#define HAVE_GETADDRINFO' << \!
#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netdb.h>
int main(void){
   struct addrinfo a, *ap;
   int lrv;

   switch((lrv = getaddrinfo("foo", "0", &a, &ap))){
   case EAI_NONAME:
   case EAI_SERVICE:
   default:
      fprintf(stderr, "%s\n", gai_strerror(lrv));
   case 0:
      break;
   }
   return 0;
}
!
fi

if feat_yes SOCKETS && [ -z "${have_getaddrinfo}" ]; then
   compile_check arpa_inet_h '<arpa/inet.h>' \
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
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
int main(void){
   struct sockaddr_in servaddr;
   unsigned short portno;
   struct servent *ep;
   struct hostent *hp;
   struct in_addr **pptr;

   portno = 0;
   if((ep = getservbyname("POPPY-PORT", "tcp")) != NULL)
      portno = (unsigned short)ep->s_port;

   if((hp = gethostbyname("POPPY-HOST")) != NULL){
      pptr = (struct in_addr**)hp->h_addr_list;
      if(hp->h_addrtype != AF_INET)
         fprintf(stderr, "au\n");
   }else{
      switch(h_errno){
      case HOST_NOT_FOUND:
      case TRY_AGAIN:
      case NO_RECOVERY:
      case NO_DATA:
         break;
      default:
         fprintf(stderr, "au\n");
         break;
      }
   }

   memset(&servaddr, 0, sizeof servaddr);
   servaddr.sin_family = AF_INET;
   servaddr.sin_port = htons(portno);
   memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
   fprintf(stderr, "Would connect to %s:%d ...\n",
      inet_ntoa(**pptr), (int)portno);
   return 0;
}
!

   < ${tmp2}.c link_check gethostbyname 'get(serv|host)byname(3)' ||
      < ${tmp2}.c link_check gethostbyname \
         'get(serv|host)byname(3) (via -nsl)' '' '-lnsl' ||
      < ${tmp2}.c link_check gethostbyname \
         'get(serv|host)byname(3) (via -lsocket -nsl)' \
         '' '-lsocket -lnsl' ||
      feat_bail_required SOCKETS
fi

feat_yes SOCKETS &&
run_check setsockopt 'setsockopt(2)' '#define HAVE_SETSOCKOPT' << \!
#include <sys/socket.h>
#include <stdlib.h>
# include <errno.h>
int main(void){
   int sockfd = 3;

   if(setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, NULL, 0) == -1 &&
         errno == ENOSYS)
      return 1;
   return 0;
}
!

feat_yes SOCKETS && [ -n "${have_setsockopt}" ] &&
link_check so_sndtimeo 'SO_SNDTIMEO' '#define HAVE_SO_SNDTIMEO' << \!
#include <sys/socket.h>
#include <stdlib.h>
int main(void){
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
link_check so_linger 'SO_LINGER' '#define HAVE_SO_LINGER' << \!
#include <sys/socket.h>
#include <stdlib.h>
int main(void){
   struct linger li;
   int sockfd = 3;

   li.l_onoff = 1;
   li.l_linger = 42;
   setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &li, sizeof li);
   return 0;
}
!

if feat_yes SSL; then
   if link_check openssl 'OpenSSL (new style *_client_method(3ssl))' \
      '#define HAVE_SSL
      #define HAVE_OPENSSL 10100' '-lssl -lcrypto' << \!
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rand.h>
#ifdef OPENSSL_NO_TLS1 /* TODO only deduced from OPENSSL_NO_SSL[23]! */
# error We need TLSv1.
#endif
int main(void){
   SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());

   SSL_CTX_free(ctx);
   PEM_read_PrivateKey(0, 0, 0, 0);
   return 0;
}
!
   then
      :
   elif link_check openssl 'OpenSSL (old style *_client_method(3ssl))' \
      '#define HAVE_SSL
      #define HAVE_OPENSSL 10000' '-lssl -lcrypto' << \!
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rand.h>
#if defined OPENSSL_NO_SSL3 &&\
      defined OPENSSL_NO_TLS1 /* TODO only deduced from OPENSSL_NO_SSL[23]! */
# error We need one of SSLv3 and TLSv1.
#endif
int main(void){
   SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());

   SSL_CTX_free(ctx);
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
      compile_check stack_of 'OpenSSL STACK_OF()' \
         '#define HAVE_OPENSSL_STACK_OF' << \!
#include <stdio.h> /* For C89 NULL */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rand.h>
int main(void){
   STACK_OF(GENERAL_NAME) *gens = NULL;

   printf("%p", gens); /* to use it */
   return 0;
}
!

      link_check ossl_conf 'OpenSSL_modules_load_file() support' \
         '#define HAVE_OPENSSL_CONFIG' << \!
#include <stdio.h> /* For C89 NULL */
#include <openssl/conf.h>
int main(void){
   CONF_modules_load_file(NULL, NULL, CONF_MFLAGS_IGNORE_MISSING_FILE);
   CONF_modules_free();
   return 0;
}
!

      link_check ossl_conf_ctx 'OpenSSL SSL_CONF_CTX support' \
         '#define HAVE_OPENSSL_CONF_CTX' << \!
#include "config.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
int main(void){
#if HAVE_OPENSSL < 10100
   SSL_CTX *ctx = SSL_CTX_new(SSLv23_client_method());
#else
   SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
#endif
   SSL_CONF_CTX *cctx = SSL_CONF_CTX_new();

   SSL_CONF_CTX_set_flags(cctx,
      SSL_CONF_FLAG_FILE | SSL_CONF_FLAG_CLIENT |
      SSL_CONF_FLAG_CERTIFICATE | SSL_CONF_FLAG_SHOW_ERRORS);
   SSL_CONF_CTX_set_ssl_ctx(cctx, ctx);
   SSL_CONF_cmd(cctx, "Protocol", "ALL");
   SSL_CONF_CTX_finish(cctx);
   SSL_CONF_CTX_free(cctx);
   SSL_CTX_free(ctx);
   return 0;
}
!

      link_check rand_egd 'OpenSSL RAND_egd()' \
         '#define HAVE_OPENSSL_RAND_EGD' << \!
#include <openssl/rand.h>
int main(void){
   return RAND_egd("some.where") > 0;
}
!

      if feat_yes SSL_ALL_ALGORITHMS; then
         if link_check ssl_all_algo 'OpenSSL all-algorithms support' \
            '#define HAVE_SSL_ALL_ALGORITHMS' << \!
#include <openssl/evp.h>
int main(void){
   OpenSSL_add_all_algorithms();
   EVP_get_cipherbyname("two cents i never exist");
   EVP_cleanup();
   return 0;
}
!
         then
            :
         else
            feat_bail_required SSL_ALL_ALGORITHMS
         fi
      fi # SSL_ALL_ALGORITHMS

      if feat_yes MD5 && feat_no NOEXTMD5; then
         run_check openssl_md5 'MD5 digest in OpenSSL' \
            '#define HAVE_OPENSSL_MD5' << \!
#include <stdlib.h>
#include <string.h>
#include <openssl/md5.h>
int main(void){
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
   for(i = 0; i < sizeof(hex) / 2; i++){
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

if feat_yes GSSAPI; then
   ${cat} > ${tmp2}.c << \!
#include <gssapi/gssapi.h>
int main(void){
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
      i='GSS-API via krb5-config(1)'
   else
      GSS_LIBS='-lgssapi'
      GSS_INCS=
      i='GSS-API in gssapi/gssapi.h, libgssapi'
   fi
   if < ${tmp2}.c link_check gss \
         "${i}" '#define HAVE_GSSAPI' "${GSS_LIBS}" "${GSS_INCS}" ||\
      < ${tmp3}.c link_check gss \
         'GSS-API in gssapi.h, libgssapi' \
         '#define HAVE_GSSAPI
         #define GSSAPI_REG_INCLUDE' \
         '-lgssapi' ||\
      < ${tmp2}.c link_check gss 'GSS-API in libgssapi_krb5' \
         '#define HAVE_GSSAPI' \
         '-lgssapi_krb5' ||\
      < ${tmp3}.c link_check gss \
         'GSS-API in libgssapi, OpenBSD-style (pre 5.3)' \
         '#define HAVE_GSSAPI
         #define GSS_REG_INCLUDE' \
         '-lgssapi -lkrb5 -lcrypto' \
         '-I/usr/include/kerberosV' ||\
      < ${tmp2}.c link_check gss 'GSS-API in libgss' \
         '#define HAVE_GSSAPI' \
         '-lgss' ||\
      link_check gss 'GSS-API in libgssapi_krb5, old-style' \
         '#define HAVE_GSSAPI
         #define GSSAPI_OLD_STYLE' \
         '-lgssapi_krb5' << \!
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_generic.h>
int main(void){
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
   if link_check idna 'GNU Libidn' '#define HAVE_IDNA HAVE_IDNA_LIBIDNA' \
         '-lidn' << \!
#include <idna.h>
#include <idn-free.h>
#include <stringprep.h>
int main(void){
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
   elif link_check idna 'idnkit' '#define HAVE_IDNA HAVE_IDNA_IDNKIT' \
         '-lidnkit' << \!
#include <stdio.h>
#include <idn/api.h>
#include <idn/result.h>
int main(void){
   idn_result_t r;
   char ace_name[256];
   char local_name[256];

   r = idn_encodename(IDN_ENCODE_APP, "does.this.work", ace_name,
         sizeof(ace_name));
   if (r != idn_success) {
      fprintf(stderr, "idn_encodename failed: %s\n", idn_result_tostring(r));
      return 1;
   }
   r = idn_decodename(IDN_DECODE_APP, ace_name, local_name, sizeof(local_name));
   if (r != idn_success) {
      fprintf(stderr, "idn_decodename failed: %s\n", idn_result_tostring(r));
      return 1;
   }
   return 0;
}
!
   then
      :
   else
      feat_bail_required IDNA
   fi

   if [ -n "${have_idna}" ]; then
      echo '#define HAVE_IDNA_LIBIDNA 0' >> ${h}
      echo '#define HAVE_IDNA_IDNKIT 1' >> ${h}
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
   if link_check regex 'regular expressions' '#define HAVE_REGEX' << \!
#include <regex.h>
#include <stdlib.h>
int main(void){
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
int main(void){
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

if feat_yes NCL && [ -z "${have_readline}" ] &&\
      [ -n "${have_c90amend1}" ]; then
   have_ncl=1
   echo '#define HAVE_NCL' >> ${h}
else
   feat_bail_required NCL
   echo '/* WANT_{READLINE,NCL}=0 */' >> ${h}
fi

# Generic have-a-command-line-editor switch for those who need it below
if [ -n "${have_ncl}" ] ||\
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

if feat_yes TERMCAP; then
   __termlib() {
      link_check termcap "for termcap(3) (via ${4})" \
         "#define HAVE_TERMCAP${3}" "${1}" << _EOT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
${2}
#include <term.h>
#define PTR2SIZE(X) ((unsigned long)(X))
#define UNCONST(P) ((void*)(unsigned long)(void const*)(P))
static char *_termcap_buffer, *_termcap_ti, *_termcap_te;
int main(void){
   char buf[1024+512], cmdbuf[2048], *cpb, *cpti, *cpte, *cp;

   tgetent(buf, getenv("TERM"));
   cpb = cmdbuf;
   cpti = cpb;
   if ((cp = tgetstr(UNCONST("ti"), &cpb)) == NULL)
      goto jleave;
   cpte = cpb;
   if ((cp = tgetstr(UNCONST("te"), &cpb)) == NULL)
      goto jleave;
   _termcap_buffer = malloc(PTR2SIZE(cpb - cmdbuf));
   memcpy(_termcap_buffer, cmdbuf, PTR2SIZE(cpb - cmdbuf));
   _termcap_ti = _termcap_buffer + PTR2SIZE(cpti - cmdbuf);
   _termcap_te = _termcap_ti + PTR2SIZE(cpte - cpti);
   tputs(_termcap_ti, 1, &putchar);
   tputs(_termcap_te, 1, &putchar);
jleave:
   return (cp == NULL);
}
_EOT
   }

   __termlib -ltermcap '' '' termcap ||
      __termlib -ltermcap '#include <curses.h>' '
         #define HAVE_TERMCAP_CURSES' \
         'curses.h / -ltermcap' ||
      __termlib -lcurses '#include <curses.h>' '
         #define HAVE_TERMCAP_CURSES' \
         'curses.h / -lcurses' ||
      feat_bail_required TERMCAP
else
   echo '/* WANT_TERMCAP=0 */' >> ${h}
fi

if feat_yes ERRORS; then
   echo '#define HAVE_ERRORS' >> ${h}
else
   echo '/* WANT_ERRORS=0 */' >> ${h}
fi

##

if feat_yes SPAM_SPAMC; then
   echo '#define HAVE_SPAM_SPAMC' >> ${h}
   if command -v spamc >/dev/null 2>&1; then
      echo "#define SPAM_SPAMC_PATH \"`command -v spamc`\"" >> ${h}
   fi
else
   echo '/* WANT_SPAM_SPAMC=0 */' >> ${h}
fi

if feat_yes SPAM_SPAMD && [ -n "${have_af_unix}" ]; then
   echo '#define HAVE_SPAM_SPAMD' >> ${h}
else
   feat_bail_required SPAM_SPAMD
   echo '/* WANT_SPAM_SPAMD=0 */' >> ${h}
fi

if feat_yes SPAM_FILTER; then
   echo '#define HAVE_SPAM_FILTER' >> ${h}
else
   echo '/* WANT_SPAM_FILTER=0 */' >> ${h}
fi

if feat_yes SPAM_SPAMC || feat_yes SPAM_SPAMD || feat_yes SPAM_FILTER; then
   echo '#define HAVE_SPAM' >> ${h}
else
   echo '/* HAVE_SPAM */' >> ${h}
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

if feat_yes FILTER_HTML_TAGSOUP; then
   echo '#define HAVE_FILTER_HTML_TAGSOUP' >> ${h}
else
   echo '/* WANT_FILTER_HTML_TAGSOUP=0 */' >> ${h}
fi

if feat_yes COLOUR; then
   echo '#define HAVE_COLOUR' >> ${h}
else
   echo '/* WANT_COLOUR=0 */' >> ${h}
fi

if feat_yes DOTLOCK; then
   echo '#define HAVE_DOTLOCK' >> ${h}
else
   echo '/* WANT_DOTLOCK=0 */' >> ${h}
fi

if feat_yes MD5; then
   echo '#define HAVE_MD5' >> ${h}
else
   echo '/* WANT_MD5=0 */' >> ${h}
fi

## Summarizing

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
printf '#ifndef n_CONFIG_H\n# define n_CONFIG_H 1\n' > ${h}
${cat} ${tmp} >> ${h}
${rm} -f ${tmp}

printf '\n/* The "feature string" */\n' >> ${h}
printf '# if defined _ACCMACVAR_SOURCE || defined HAVE_AMALGAMATION\n' >> ${h}
printf 'static char const _features[] = "MIME"\n' >> ${h}
printf '# ifdef HAVE_SETLOCALE\n   ",LOCALES"\n# endif\n' >> ${h}
printf '# ifdef HAVE_C90AMEND1\n   ",MULTIBYTE CHARSETS"\n# endif\n' >> ${h}
printf '# ifdef HAVE_NL_LANGINFO\n   ",TERMINAL CHARSET"\n# endif\n' >> ${h}
printf '# ifdef HAVE_ICONV\n   ",ICONV"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SOCKETS\n   ",NETWORK"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SSL\n   ",S/MIME,SSL/TLS"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SSL_ALL_ALGORITHMS\n   ",SSL-ALL-ALGORITHMS"\n# endif\n'\
   >> ${h}
printf '# ifdef HAVE_SMTP\n   ",SMTP"\n# endif\n' >> ${h}
printf '# ifdef HAVE_POP3\n   ",POP3"\n# endif\n' >> ${h}
printf '# ifdef HAVE_GSSAPI\n   ",GSS-API"\n# endif\n' >> ${h}
printf '# ifdef HAVE_MD5\n   ",MD5 [APOP,CRAM-MD5]"\n# endif\n' >> ${h}
printf '# ifdef HAVE_NETRC\n   ",NETRC"\n# endif\n' >> ${h}
printf '# ifdef HAVE_AGENT\n   ",AGENT"\n# endif\n' >> ${h}
printf '# ifdef HAVE_IDNA\n   ",IDNA"\n# endif\n' >> ${h}
printf '# ifdef HAVE_IMAP_SEARCH\n   ",IMAP-SEARCH"\n# endif\n' >> ${h}
printf '# ifdef HAVE_REGEX\n   ",REGEX"\n# endif\n' >> ${h}
printf '# ifdef HAVE_READLINE\n   ",READLINE"\n# endif\n' >> ${h}
printf '# ifdef HAVE_NCL\n   ",NCL"\n# endif\n' >> ${h}
printf '# ifdef HAVE_TABEXPAND\n   ",TABEXPAND"\n# endif\n' >> ${h}
printf '# ifdef HAVE_HISTORY\n   ",HISTORY"\n# endif\n' >> ${h}
printf '# ifdef HAVE_TERMCAP\n   ",TERMCAP"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SPAM_SPAMC\n   ",SPAMC"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SPAM_SPAMD\n   ",SPAMD"\n# endif\n' >> ${h}
printf '# ifdef HAVE_SPAM_FILTER\n   ",SPAMFILTER"\n# endif\n' >> ${h}
printf '# ifdef HAVE_DOCSTRINGS\n   ",DOCSTRINGS"\n# endif\n' >> ${h}
printf '# ifdef HAVE_QUOTE_FOLD\n   ",QUOTE-FOLD"\n# endif\n' >> ${h}
printf '# ifdef HAVE_FILTER_HTML_TAGSOUP\n   ",HTML-FILTER"\n# endif\n' >> ${h}
printf '# ifdef HAVE_COLOUR\n   ",COLOUR"\n# endif\n' >> ${h}
printf '# ifdef HAVE_DOTLOCK\n   ",DOTLOCK-FILES"\n# endif\n' >> ${h}
printf '# ifdef HAVE_DEBUG\n   ",DEBUG"\n# endif\n' >> ${h}
printf '# ifdef HAVE_DEVEL\n   ",DEVEL"\n# endif\n' >> ${h}
printf ';\n# endif /* _ACCMACVAR_SOURCE || HAVE_AMALGAMATION */\n' >> ${h}

# Create the real mk.mk
# Note we cannout use explicit ./ filename prefix for source and object
# pathnames because of a bug in bmake(1)
${rm} -rf ${tmp0}.* ${tmp0}*
printf 'OBJ_SRC = ' >> ${mk}
if feat_no AMALGAMATION; then
   for i in `printf '%s\n' *.c | ${sort}`; do
      if [ "${i}" = privsep.c ]; then
         continue
      fi
      printf "${i} " >> ${mk}
   done
   printf '\nAMALGAM_TARGET =\nAMALGAM_DEP =\n' >> ${mk}
else
   printf 'main.c\nAMALGAM_TARGET = main.o\nAMALGAM_DEP = ' >> ${mk}

   printf '\n/* HAVE_AMALGAMATION: include sources */\n' >> ${h}
   printf '#elif _CONFIG_H + 0 == 1\n' >> ${h}
   printf '# undef _CONFIG_H\n' >> ${h}
   printf '# define _CONFIG_H 2\n' >> ${h}
   for i in `printf '%s\n' *.c | ${sort}`; do
      if [ "${i}" = "${j}" ] || [ "${i}" = main.c ] || \
            [ "${i}" = privsep.c ]; then
         continue
      fi
      printf "${i} " >> ${mk}
      printf "# include \"${i}\"\n" >> ${h}
   done
   echo >> ${mk}
   # tcc(1) fails on 2015-11-13 unless this #else clause existed
   echo '#else' >> ${h}
fi

printf '#endif /* n_CONFIG_H */\n' >> ${h}

echo "LIBS = `${cat} ${lib}`" >> ${mk}
echo "INCS = `${cat} ${inc}`" >> ${mk}
echo >> ${mk}
${cat} ./mk-mk.in >> ${mk}

## Finished!

${cat} > ${tmp2}.c << \!
#include "config.h"
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
#ifdef HAVE_SSL
# ifdef HAVE_OPENSSL
: + S/MIME and SSL/TLS (OpenSSL)
# endif
# ifdef HAVE_SSL_ALL_ALGORITHMS
: + + Support for more ("all") digest and cipher algorithms
# endif
#endif
#ifdef HAVE_SMTP
: + SMTP protocol
#endif
#ifdef HAVE_POP3
: + POP3 protocol
#endif
#ifdef HAVE_GSSAPI
: + GSS-API authentication
#endif
#ifdef HAVE_MD5
: + MD5 message digest (APOP, CRAM-MD5)
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
#if defined HAVE_READLINE || defined HAVE_NCL
# ifdef HAVE_READLINE
: + Command line editing via readline(3)
# else
: + Command line editing via N(ail) C(ommand) L(ine)
# endif
# ifdef HAVE_TABEXPAND
: + + Tabulator expansion
# endif
# ifdef HAVE_HISTORY
: + + History management
# endif
#endif
#ifdef HAVE_TERMCAP
: + Terminal capability queries
#endif
#ifdef HAVE_SPAM
: + Spam management
# ifdef HAVE_SPAM_SPAMC
: + + Via spamc(1) (of spamassassin(1))
# endif
# ifdef HAVE_SPAM_SPAMD
: + + Directly via spamd(1) (of spamassassin(1))
# endif
# ifdef HAVE_SPAM_FILTER
: + + Via freely configurable *spam-filter-XY*s
# endif
#endif
#ifdef HAVE_DOCSTRINGS
: + Documentation summary strings
#endif
#ifdef HAVE_QUOTE_FOLD
: + Extended *quote-fold*ing
#endif
#ifdef HAVE_FILTER_HTML_TAGSOUP
: + Builtin HTML-to-text filter (for display purposes, primitive)
#endif
#ifdef HAVE_COLOUR
: + Coloured message display (simple)
#endif
#ifdef HAVE_DOTLOCK
: + Dotlock files and privilege-separated file dotlock program
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
: _ (Ooooh, no iconv(3), NO character set conversion possible!  Really...)
#endif
#ifndef HAVE_SOCKETS
: - Network support
#endif
#ifndef HAVE_SSL
: - S/MIME and SSL/TLS
#else
# ifndef HAVE_SSL_ALL_ALGORITHMS
: - Support for more S/MIME and SSL/TLS digest and cipher algorithms
# endif
#endif
#ifndef HAVE_SMTP
: - SMTP protocol
#endif
#ifndef HAVE_POP3
: - POP3 protocol
#endif
#ifndef HAVE_GSSAPI
: - GSS-API authentication
#endif
#ifndef HAVE_MD5
: - MD5 message digest (APOP, CRAM-MD5)
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
#if !defined HAVE_READLINE && !defined HAVE_NCL
: - Command line editing and history
#endif
#ifndef HAVE_TERMCAP
: - Terminal capability queries
#endif
#ifndef HAVE_SPAM
: - Spam management
#endif
#ifndef HAVE_DOCSTRINGS
: - Documentation summary strings
#endif
#ifndef HAVE_QUOTE_FOLD
: - Extended *quote-fold*ing
#endif
#ifndef HAVE_FILTER_HTML_TAGSOUP
: - Builtin HTML-to-text filter (for display purposes, primitive)
#endif
#ifndef HAVE_COLOUR
: - Coloured message display (simple)
#endif
#ifndef HAVE_DOTLOCK
: - Dotlock files and privilege-separated file dotlock program
#endif
:
#if !defined HAVE_WORDEXP || !defined HAVE_FCHDIR ||\
      defined HAVE_DEBUG || defined HAVE_DEVEL
:Remarks:
# ifndef HAVE_WORDEXP
: . WARNING: the function wordexp(3) could not be found.
: _ This means that echo(1) will be used via the sh(1)ell in order
: _ to expand shell meta characters in filenames, which is a potential
: _ security hole.  Consider to either upgrade your system or set the
: _ *SHELL* variable to some safe(r) wrapper script.
: _ P.S.: the codebase is in transition away from wordexp(3) to some
: _ safe (restricted) internal mechanism, see "COMMANDS" manual, read
: _ about shell word expression in its introduction for more on that.
# endif
# ifndef HAVE_FCHDIR
: . The function fchdir(2) could not be found. We will use chdir(2)
: _ instead. This is not a problem unless the current working
: _ directory is changed while this program is inside of it.
# endif
# ifdef HAVE_DEBUG
: . Debug enabled binary: not meant to be used by end-users: THANKS!
# endif
# ifdef HAVE_DEVEL
: . Computers do not blunder.
# endif
:
#endif /* Remarks */
:Setup:
: . System-wide resource file: SYSCONFDIR/SYSCONFRC
: . bindir: BINDIR
#ifdef HAVE_DOTLOCK
: . libexecdir: LIBEXECDIR
#endif
: . mandir: MANDIR
: . sendmail(1): SENDMAIL (argv[0] = SENDMAIL_PROGNAME)
: . Mail spool directory: MAILSPOOL
:
!

${make} -f ${makefile} ${tmp2}.x
< ${tmp2}.x ${sed} -e '/^[^:]/d; /^$/d; s/^://' |
while read l; do
   msg "${l}"
done

# s-it-mode
