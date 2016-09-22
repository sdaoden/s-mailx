#!/bin/sh -
#@ Please see INSTALL and make.rc instead.

LC_ALL=C
export LC_ALL

# The feature set, to be kept in sync with make.rc
# If no documentation given, the option is used as such; if doc is a hyphen,
# entry is suppressed when configuration overview is printed: most likely for
# obsolete features etc.
XOPTIONS="\
   ICONV='Character set conversion using iconv(3)' \
   SOCKETS='Network support' \
      SSL='SSL/TLS (OpenSSL)' \
         SSL_ALL_ALGORITHMS='Support of all digest and cipher algorithms' \
      SMTP='Simple Mail Transfer Protocol client' \
      POP3='Post Office Protocol Version 3 client' \
      GSSAPI='Generic Security Service authentication' \
      NETRC='.netrc file support' \
      AGENT='-' \
      MD5='MD5 message digest (APOP, CRAM-MD5)' \
   IDNA='Internationalized Domain Names for Applications (encode only)' \
   IMAP_SEARCH='IMAP-style search expressions' \
   REGEX='Regular expressions' \
   MLE='Mailx Line Editor' \
      HISTORY='Line editor history management' \
      KEY_BINDINGS='Configurable key bindings' \
   TERMCAP='Terminal capability queries (termcap(5))' \
      TERMCAP_VIA_TERMINFO='Terminal capability queries use terminfo(5)' \
   ERRORS='Error log message ring' \
   SPAM_SPAMC='Spam management via spamc(1) of spamassassin(1)' \
   SPAM_SPAMD='-' \
   SPAM_FILTER='Freely configurable *spam-filter-..*s' \
   DOCSTRINGS='Command documentation help strings' \
   QUOTE_FOLD='Extended *quote-fold*ing' \
   FILTER_HTML_TAGSOUP='Simple builtin HTML-to-text display filter' \
   COLOUR='Coloured message display' \
   DOTLOCK='Dotlock files and privilege-separated dotlock program' \
"

# Options which are automatically deduced from host environment, i.e., these
# need special treatment all around here to warp from/to OPT_ stuff
# setlocale, C90AMEND1, NL_LANGINFO, wcwidth
XOPTIONS_DETECT="\
   LOCALES='Locale support - printable characters etc. depend on environment' \
   MBYTE_CHARSETS='Multibyte character sets' \
   TERMINAL_CHARSET='Automatic detection of terminal character set' \
   WIDE_GLYPHS='Wide glyph support' \
"

# Rather special options, for custom building, or which always exist.
# Mostly for generating the visual overview and the *feature* string
XOPTIONS_XTRA="\
   MIME='Multipurpose Internet Mail Extensions' \
   SMIME='S/MIME message signing, verification, en- and decryption' \
   CROSS_BUILD='Cross-compilation: trust any detected environment' \
   DEBUG='Debug enabled binary, not for end-users: THANKS!' \
   DEVEL='Computers do not blunder' \
"

# The problem is that we don't have any tools we can use right now, so
# encapsulate stuff in functions which get called in right order later on

option_reset() {
   set -- ${OPTIONS}
   for i
   do
      eval OPT_${i}=0
   done
}

option_maximal() {
   set -- ${OPTIONS}
   for i
   do
      eval OPT_${i}=1
   done
   OPT_ICONV=require
   OPT_REGEX=require
   OPT_DOTLOCK=require
}

option_setup() {
   option_parse OPTIONS_DETECT "${XOPTIONS_DETECT}"
   option_parse OPTIONS "${XOPTIONS}"
   option_parse OPTIONS_XTRA "${XOPTIONS_XTRA}"
   OPT_MIME=1

   # Predefined CONFIG= urations take precedence over anything else
   if [ -n "${CONFIG}" ]; then
      case "${CONFIG}" in
      [nN][uU][lL][lL])
         option_reset
         ;;
      [nN][uU][lL][lL][iI])
         option_reset
         OPT_ICONV=require
         ;;
      [mM][iI][nN][iI][mM][aA][lL])
         option_reset
         OPT_ICONV=1
         OPT_REGEX=1
         OPT_DOTLOCK=require
         ;;
      [mM][eE][dD][iI][uU][mM])
         option_reset
         OPT_ICONV=require
         OPT_IDNA=1
         OPT_REGEX=1
         OPT_MLE=1
            OPT_HISTORY=1 OPT_KEY_BINDINGS=1
         OPT_ERRORS=1
         OPT_SPAM_FILTER=1
         OPT_DOCSTRINGS=1
         OPT_COLOUR=1
         OPT_DOTLOCK=require
         ;;
      [nN][eE][tT][sS][eE][nN][dD])
         option_reset
         OPT_ICONV=require
         OPT_SOCKETS=1
            OPT_SSL=require
            OPT_SMTP=require
            OPT_GSSAPI=1 OPT_NETRC=1 OPT_AGENT=1
         OPT_IDNA=1
         OPT_REGEX=1
         OPT_MLE=1
            OPT_HISTORY=1 OPT_KEY_BINDINGS=1
         OPT_DOCSTRINGS=1
         OPT_COLOUR=1
         OPT_DOTLOCK=require
         ;;
      [mM][aA][xX][iI][mM][aA][lL])
         option_reset
         option_maximal
         ;;
      [dD][eE][vV][eE][lL])
         OPT_DEVEL=1 OPT_DEBUG=1 OPT_NYD2=1
         option_maximal
         ;;
      [oO][dD][eE][vV][eE][lL])
         OPT_DEVEL=1
         option_maximal
         ;;
      *)
         echo >&2 "Unknown CONFIG= setting: ${CONFIG}"
         echo >&2 '   NULL, NULLI, MINIMAL, MEDIUM, NETSEND, MAXIMAL'
         exit 1
         ;;
      esac
   fi
}

# Inter-relationships
option_update() {
   if feat_no SMTP && feat_no POP3; then
      OPT_SOCKETS=0
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
      OPT_SSL=0 OPT_SSL_ALL_ALGORITHMS=0
      OPT_SMTP=0 OPT_POP3=0
      OPT_GSSAPI=0 OPT_NETRC=0 OPT_AGENT=0
   fi
   if feat_no SMTP; then
      OPT_GSSAPI=0
   fi

   if feat_no MLE; then
      OPT_HISTORY=0 OPT_KEY_BINDINGS=0
   fi

   # If we don't need MD5 leave it alone
   if feat_no SOCKETS; then
      OPT_MD5=0
   fi

   if feat_yes DEVEL; then
      OPT_DEBUG=1
   fi
   if feat_yes DEBUG; then
      OPT_NOALLOCA=1 OPT_DEVEL=1
   fi
}

rc=./make.rc
lst=./config.lst
ev=./config.ev
h=./config.h h_name=config.h
mk=./mk.mk

newlst=./config.lst-new
newmk=./config.mk-new
newev=./config.ev-new
newh=./config.h-new
tmp0=___tmp
tmp=./${tmp0}1$$
tmp2=./${tmp0}2$$

##  --  >8  - << OPTIONS | OS/CC >> -  8<  --  ##

# Note that potential duplicates in PATH, C_INCLUDE_PATH etc. will be cleaned
# via path_check() later on once possible

# TODO cc_maxopt is brute simple, we should compile test program and dig real
# compiler versions for known compilers, then be more specific
cc_maxopt=100
_CFLAGS= _LDFLAGS=

os_early_setup() {
   # We don't "have any utility": only path adjustments and such in here!
   i="${OS:-`uname -s`}"

   if [ ${i} = SunOS ]; then
      msg 'SunOS / Solaris?  Applying some "early setup" rules ...'
      _os_early_setup_sunos
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

os_setup() {
   # OSFULLSPEC is used to recognize changes (i.e., machine type, updates etc.)
   OSFULLSPEC="${OS:-`uname -a | ${tr} '[A-Z]' '[a-z]'`}"
   OS="${OS:-`uname -s | ${tr} '[A-Z]' '[a-z]'`}"
   msg 'Operating system is %s' ${OS}

   if [ ${OS} = sunos ]; then
      msg ' . have special SunOS / Solaris "setup" rules ...'
      _os_setup_sunos
   elif [ ${OS} = unixware ]; then
      msg ' . have special UnixWare environmental rules ...'
      if feat_yes AUTOCC && command -v cc >/dev/null 2>&1; then
         CC=cc
         feat_yes DEBUG && _CFLAGS='-v -Xa -g' || _CFLAGS='-Xa -O'

         CFLAGS="${_CFLAGS} ${EXTRA_CFLAGS}"
         LDFLAGS="${_LDFLAGS} ${EXTRA_LDFLAGS}"
         export CC CFLAGS LDFLAGS
         OPT_AUTOCC=0 had_want_autocc=1 need_R_ldflags=-R
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
      msg 'ERROR: Not an executable program: %s' "${cksum}"
      msg 'ERROR:   We need a CRC-32 cksum(1), as specified in POSIX.'
      msg 'ERROR:   However, we do so only for tests.'
      msg 'ERROR:   If that is ok, set "cksum=/usr/bin/true", then rerun'
      config_exit 1
   fi

   if feat_yes AUTOCC; then
      if command -v cc >/dev/null 2>&1; then
         CC=cc
         feat_yes DEBUG && _CFLAGS="-v -Xa -g" || _CFLAGS="-Xa -O"

         CFLAGS="${_CFLAGS} ${EXTRA_CFLAGS}"
         LDFLAGS="${_LDFLAGS} ${EXTRA_LDFLAGS}"
         export CC CFLAGS LDFLAGS
         OPT_AUTOCC=0 had_want_autocc=1 need_R_ldflags=-R
      else
         # Assume gcc(1), which supports -R for compat
         cc_maxopt=2 force_no_stackprot=1 need_R_ldflags=-Wl,-R
      fi
   fi
}

# Check out compiler ($CC) and -flags ($CFLAGS)
cc_setup() {
   # Even though it belongs into cc_flags we will try to compile and link
   # something, so ensure we have a clean state regarding CFLAGS/LDFLAGS or
   # EXTRA_CFLAGS/EXTRA_LDFLAGS
   if feat_no AUTOCC; then
      _cc_default
      # Ensure those don't do any harm
      EXTRA_CFLAGS= EXTRA_LDFLAGS=
      export EXTRA_CFLAGS EXTRA_LDFLAGS
      return
   else
      CFLAGS= LDFLAGS=
      export CFLAGS LDFLAGS
   fi

   [ -n "${CC}" ] && [ "${CC}" != cc ] && { _cc_default; return; }

   msg_nonl 'Searching for a usable C compiler .. $CC='
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
         msg 'boing booom tschak'
         msg 'ERROR: I cannot find a compiler!'
         msg ' Neither of clang(1), gcc(1), tcc(1), pcc(1), c89(1) and c99(1).'
         msg ' Please set ${CC} environment variable, maybe ${CFLAGS}, rerun.'
         config_exit 1
      fi
   fi
   msg '%s' "${CC}"
   export CC
}

_cc_default() {
   if [ -z "${CC}" ]; then
      msg 'To go on like you have chosen, please set $CC, rerun.'
      config_exit 1
   fi

   if [ -z "${VERBOSE}" ] && [ -f ${lst} ] && feat_no DEBUG; then
      :
   else
      msg 'Using C compiler ${CC}=%s' "${CC}"
   fi
}

cc_flags() {
   if feat_yes AUTOCC; then
      if [ -f ${lst} ] && feat_no DEBUG && [ -z "${VERBOSE}" ]; then
         cc_check_silent=1
         msg 'Detecting ${CFLAGS}/${LDFLAGS} for ${CC}=%s, just a second..' \
            "${CC}"
      else
         cc_check_silent=
         msg 'Testing usable ${CFLAGS}/${LDFLAGS} for ${CC}=%s' "${CC}"
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
      CFLAGS="${_CFLAGS} ${EXTRA_CFLAGS}"
      LDFLAGS="${_LDFLAGS} ${EXTRA_LDFLAGS}"
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

   if ld_check -Wl,-rpath =./ no; then
      need_R_ldflags=-Wl,-rpath=
      ld_runtime_flags # update!
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
         msg 'You may turn off OPT_AUTOCC and use your own settings, rerun'
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
   if ld_check -Wl,-rpath =./ no; then
      need_R_ldflags=-Wl,-rpath=
      ld_runtime_flags # update!
   elif ld_check -Wl,-R ./ no; then
      need_R_ldflags=-Wl,-R
      ld_runtime_flags # update!
   fi

   # Address randomization
   _ccfg=${_CFLAGS}
   if cc_check -fPIE || cc_check -fpie; then
      ld_check -pie || _CFLAGS=${_ccfg}
   fi
   unset _ccfg

   _CFLAGS="${_CFLAGS} ${__cflags}" _LDFLAGS="${_LDFLAGS} ${__ldflags}"
   unset __cflags __ldflags
}

##  --  >8  - <<OS/CC | SUPPORT FUNS>> -  8<  --  ##

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

msg_nonl() {
   fmt=${1}
   shift
   printf >&2 -- "${fmt}" "${@}"
}

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
      [ -n "${VERBOSE}" ] && msg ' . ${%s} ... %s' "${n}" "${i}"
      eval ${n}=${i}
      return 0
   fi
   if [ ${opt} -eq 0 ]; then
      msg 'ERROR: no trace of utility %s' "${n}"
      config_exit 1
   fi
   return 1
}

# Our feature check environment
feat_val_no() {
   [ "x${1}" = x0 ] || [ "x${1}" = xn ] ||
   [ "x${1}" = xfalse ] || [ "x${1}" = xno ] || [ "x${1}" = xoff ]
}

feat_val_yes() {
   [ "x${1}" = x1 ] || [ "x${1}" = xy ] ||
   [ "x${1}" = xtrue ] || [ "x${1}" = xyes ] || [ "x${1}" = xon ] ||
         [ "x${1}" = xrequire ]
}

feat_val_require() {
   [ "x${1}" = xrequire ]
}

_feat_check() {
   eval i=\$OPT_${1}
   i="`echo ${i} | ${tr} '[A-Z]' '[a-z]'`"
   if feat_val_no "${i}"; then
      return 1
   elif feat_val_yes "${i}"; then
      return 0
   else
      msg "ERROR: %s: 0/n/false/no/off or 1/y/true/yes/on/require, got: %s" \
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
   eval i=\$OPT_${1}
   i="`echo ${i} | ${tr} '[A-Z]' '[a-z]'`"
   [ "x${i}" = xrequire ] || [ "x${i}" = xrequired ]
}

feat_bail_required() {
   if feat_require ${1}; then
      msg 'ERROR: feature OPT_%s is required but not available' "${1}"
      config_exit 13
   fi
   eval OPT_${1}=0
   option_update # XXX this is rather useless here (dependency chain..)
}

option_parse() {
   # Parse one of our XOPTIONS* in $2 and assign the sh(1) compatible list of
   # options, without documentation, to $1
   j=\'
   i="`${awk} -v input=\"${2}\" '
      BEGIN{
         for(i = 0;;){
            voff = match(input, /[[:alnum:]_]+(='${j}'[^'${j}']+)?/)
            if(voff == 0)
               break
            v = substr(input, voff, RLENGTH)
            input = substr(input, voff + RLENGTH)
            doff = index(v, "=")
            if(doff > 0){
               d = substr(v, doff + 2, length(v) - doff - 1)
               v = substr(v, 1, doff - 1)
            }
            print v
         }
      }
      '`"
   eval ${1}=\"${i}\"
}

option_doc_of() {
   # Return the "documentation string" for option $1, itself if none such
   j=\'
   ${awk} -v want="${1}" \
      -v input="${XOPTIONS_DETECT}${XOPTIONS}${XOPTIONS_XTRA}" '
   BEGIN{
      for(;;){
         voff = match(input, /[[:alnum:]_]+(='${j}'[^'${j}']+)?/)
         if(voff == 0)
            break
         v = substr(input, voff, RLENGTH)
         input = substr(input, voff + RLENGTH)
         doff = index(v, "=")
         if(doff > 0){
            d = substr(v, doff + 2, length(v) - doff - 1)
            v = substr(v, 1, doff - 1)
         }else
            d = v
         if(v == want){
            if(d != "-")
               print d
            exit
         }
      }
   }
   '
}

option_join_rc() {
   # Join the values from make.rc into what currently is defined, not
   # overwriting yet existing settings
   ${rm} -f ${tmp}
   # We want read(1) to perform backslash escaping in order to be able to use
   # multiline values in make.rc; the resulting sh(1)/sed(1) code was very slow
   # in VMs (see [fa2e248]), Aharon Robbins suggested the following
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
         msg 'ERROR: invalid syntax in: %s' "${line}"
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
      [ "${i}" = "DESTDIR" ] && continue
      echo "${i}=\"${j}\""
   done > ${tmp}
   # Reread the mixed version right now
   . ./${tmp}
}

option_evaluate() {
   # Expand the option values, which may contain shell snippets
   ${rm} -f ${newlst} ${newmk} ${newh}
   exec 5<&0 6>&1 <${tmp} >${newlst}
   while read line; do
      z=
      if [ -n "${good_shell}" ]; then
         i=${line%%=*}
         [ "${i}" != "${i#OPT_}" ] && z=1
      else
         i=`${awk} -v LINE="${line}" 'BEGIN{
            gsub(/=.*$/, "", LINE);\
            print LINE
         }'`
         if echo "${i}" | ${grep} '^OPT_' >/dev/null 2>&1; then
            z=1
         fi
      fi

      eval j=\$${i}
      if [ -n "${z}" ]; then
         j="`echo ${j} | ${tr} '[A-Z]' '[a-z]'`"
         if [ -z "${j}" ] || feat_val_no "${j}"; then
            j=0
            printf "   /* #undef ${i} */\n" >> ${newh}
         elif feat_val_yes "${j}"; then
            if feat_val_require "${j}"; then
               j=require
            else
               j=1
            fi
            printf "   /* #define ${i} */\n" >> ${newh}
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
}

path_check() {
   # "path_check VARNAME" or "path_check VARNAME FLAG VARNAME"
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
      # Skip any fakeroot packager environment
      case "${i}" in *fakeroot*) continue;; esac
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

ld_runtime_flags() {
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
   # Disable it for a possible second run.
   need_R_ldflags=
}

cc_check() {
   [ -n "${cc_check_silent}" ] || msg_nonl ' . CC %s .. ' "${1}"
   if "${CC}" ${INCS} \
         ${_CFLAGS} ${1} ${EXTRA_CFLAGS} ${_LDFLAGS} ${EXTRA_LDFLAGS} \
         -o ${tmp2} ${tmp}.c ${LIBS} >/dev/null 2>&1; then
      _CFLAGS="${_CFLAGS} ${1}"
      [ -n "${cc_check_silent}" ] || msg 'yes'
      return 0
   fi
   [ -n "${cc_check_silent}" ] || msg 'no'
   return 1
}

ld_check() {
   # $1=option [$2=option argument] [$3=if set, shall NOT be added to _LDFLAGS]
   [ -n "${cc_check_silent}" ] || msg_nonl ' . LD %s .. ' "${1}"
   if "${CC}" ${INCS} ${_CFLAGS} ${_LDFLAGS} ${1}${2} ${EXTRA_LDFLAGS} \
         -o ${tmp2} ${tmp}.c ${LIBS} >/dev/null 2>&1; then
      [ -n "${3}" ] || _LDFLAGS="${_LDFLAGS} ${1}"
      [ -n "${cc_check_silent}" ] || msg 'yes'
      return 0
   fi
   [ -n "${cc_check_silent}" ] || msg 'no'
   return 1
}

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

   feat_yes CROSS_BUILD && run=0

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

feat_def() {
   if feat_yes ${1}; then
      echo '#define HAVE_'${1}'' >> ${h}
   else
      echo '/* OPT_'${1}'=0 */' >> ${h}
   fi
}

squeeze_em() {
   < "${1}" > "${2}" ${awk} \
   'BEGIN {ORS = " "} /^[^#]/ {print} {next} END {ORS = ""; print "\n"}'
}

##  --  >8  - <<SUPPORT FUNS | RUNNING>> -  8<  --  ##

# First of all, create new configuration and check whether it changed

# Very easy checks for the operating system in order to be able to adjust paths
# or similar very basic things which we need to be able to go at all
os_early_setup

# Check those tools right now that we need before including $rc
msg 'Checking for basic utility set'
check_tool awk "${awk:-`command -v awk`}"
check_tool rm "${rm:-`command -v rm`}"
check_tool tr "${tr:-`command -v tr`}"

# Initialize the option set
msg_nonl 'Setting up configuration options ... '
option_setup
msg 'done'

# Include $rc, but only take from it what wasn't overwritten by the user from
# within the command line or from a chosen fixed CONFIG=
# Note we leave alone the values
trap "exit 1" HUP INT TERM
trap "${rm} -f ${tmp}" EXIT

msg_nonl 'Joining in %s ... ' ${rc}
option_join_rc
msg 'done'

# We need to know about that now, in order to provide utility overwrites etc.
os_setup

msg 'Checking for remaining set of utilities'
check_tool grep "${grep:-`command -v grep`}"

# Before we step ahead with the other utilities perform a path cleanup first.
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

# Update OPT_ options now, in order to get possible inter-dependencies right
option_update

# (No functions since some shells loose non-exported variables in traps)
trap "trap \"\" HUP INT TERM; exit 1" HUP INT TERM
trap "trap \"\" HUP INT TERM EXIT;\
   ${rm} -rf ${newlst} ${tmp0}.* ${tmp0}* ${newmk} ${newev} ${newh}" EXIT

# Our configuration options may at this point still contain shell snippets,
# we need to evaluate them in order to get them expanded, and we need those
# evaluated values not only in our new configuration file, but also at hand..
msg_nonl 'Evaluating all configuration items ... '
option_evaluate
msg 'done'

# Add the known utility and some other variables
printf "#define VAL_UAGENT \"${VAL_SID}${VAL_NAIL}\"\n" >> ${newh}
printf "VAL_UAGENT = ${VAL_SID}${VAL_NAIL}\n" >> ${newmk}

printf "#define VAL_PRIVSEP \"${VAL_SID}${VAL_NAIL}-privsep\"\n" >> ${newh}
printf "VAL_PRIVSEP = \$(VAL_UAGENT)-privsep\n" >> ${newmk}
if feat_yes DOTLOCK; then
   printf "OPTIONAL_PRIVSEP = \$(VAL_PRIVSEP)\n" >> ${newmk}
else
   printf "OPTIONAL_PRIVSEP =\n" >> ${newmk}
fi

for i in \
      awk cat chmod chown cp cmp grep mkdir mv rm sed sort tee tr \
      MAKE MAKEFLAGS make SHELL strip \
      cksum; do
   eval j=\$${i}
   printf "${i} = ${j}\n" >> ${newmk}
   printf "${i}=${j}\n" >> ${newlst}
   printf "${i}=\"${j}\";export ${i}; " >> ${newev}
done
printf "\n" >> ${newev}

# Build a basic set of INCS and LIBS according to user environment.
path_check C_INCLUDE_PATH -I _INCS
INCS="${INCS} ${_INCS}"
path_check LD_LIBRARY_PATH -L _LIBS
LIBS="${LIBS} ${_LIBS}"
unset _INCS _LIBS
export C_INCLUDE_PATH LD_LIBRARY_PATH

# Some environments need runtime path flags to be able to go at all
ld_runtime_flags

## Detect CC, whether we can use it, and possibly which CFLAGS we can use

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
   memcpy(buf, s, strlen(s) +1);
   puts(s);
}
!

if "${CC}" ${INCS} ${CFLAGS} ${EXTRA_CFLAGS} ${LDFLAGS} ${EXTRA_LDFLAGS} \
      -o ${tmp2} ${tmp}.c ${LIBS}; then
   :
else
   msg 'ERROR: i cannot compile a "Hello world" via'
   msg '   %s' \
   "${CC} ${INCS} ${CFLAGS} ${EXTRA_CFLAGS} ${LDFLAGS} ${EXTRA_LDFLAGS} ${LIBS}"
   msg 'ERROR:   Please read INSTALL, rerun'
   config_exit 1
fi

# This may also update ld_runtime_flags() (again)
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

# Now finally check whether we already have a configuration and if so, whether
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
${mv} -f ${newev} ${ev}
${mv} -f ${newh} ${h}
${mv} -f ${newmk} ${mk}

if [ -z "${VERBOSE}" ]; then
   printf -- "ECHO_CC = @echo '  'CC \$(@);\n" >> ${mk}
   printf -- "ECHO_LINK = @echo '  'LINK \$(@);\n" >> ${mk}
   printf -- "ECHO_GEN = @echo '  'GEN \$(@);\n" >> ${mk}
   printf -- "ECHO_TEST = @\n" >> ${mk}
   printf -- "ECHO_CMD = @echo '  CMD';\n" >> ${mk}
   printf -- "ECHO_BLOCK_BEGIN = @( \n" >> ${mk}
   printf -- "ECHO_BLOCK_END = ) >/dev/null\n" >> ${mk}
fi

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

# !!
exec 5>&2 > ${log} 2>&1

echo "${LIBS}" > ${lib}
echo "${INCS}" > ${inc}
${cat} > ${makefile} << \!
.SUFFIXES: .o .c .x .y
.c.o:
	$(CC) -I./ $(XINCS) $(CFLAGS) -c $(<)
.c.x:
	$(CC) -I./ $(XINCS) -E $(<) > $(@)
.c:
	$(CC) -I./ $(XINCS) $(CFLAGS) $(LDFLAGS) -o $(@) $(<) $(XLIBS)
!

## Generics

# May be multiline..
[ -n "${OS_DEFINES}" ] && printf -- "${OS_DEFINES}" >> ${h}

feat_def AMALGAMATION
feat_def CROSS_BUILD
feat_def DEBUG
feat_def DEVEL
feat_def DOCSTRINGS
feat_def ERRORS
feat_def NYD2
feat_def NOMEMDBG

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

## Test for "basic" system-calls / functionality that is used by all parts
## of our program.  Once this is done fork away BASE_LIBS and other BASE_*
## macros to be used by only the subprograms (potentially).

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

if link_check ftruncate 'ftruncate(2)' \
   '#define HAVE_FTRUNCATE' << \!
#include <unistd.h>
#include <sys/types.h>
int main(void){
   return (ftruncate(0, 0) != 0);
}
!
then
   :
else
   msg 'ERROR: we require the ftruncate(2) system call.'
   config_exit 1
fi

if run_check sa_restart 'SA_RESTART (for sigaction(2))' << \!
#include <signal.h>
# include <errno.h>
int main(void){
   struct sigaction nact, oact;

   nact.sa_handler = SIG_DFL;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = SA_RESTART;
   return !(!sigaction(SIGCHLD, &nact, &oact) || errno != ENOSYS);
}
!
then
   :
else
   msg 'ERROR: we (yet) require the SA_RESTART flag for sigaction(2).'
   config_exit 1
fi

if link_check snprintf 'snprintf(3)' << \!
#include <stdio.h>
int main(void){
   char b[20];

   snprintf(b, sizeof b, "%s", "string");
   return 0;
}
!
then
   :
else
   msg 'ERROR: we require the snprintf(3) function.'
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

if link_check setenv '(un)?setenv(3)' '#define HAVE_SETENV' << \!
#include <stdlib.h>
int main(void){
   setenv("s-mailx", "i want to see it cute!", 1);
   unsetenv("s-mailx");
   return 0;
}
!
then
   :
elif link_check setenv 'putenv(3)' '#define HAVE_PUTENV' << \!
#include <stdlib.h>
int main(void){
   putenv("s-mailx=i want to see it cute!");
   return 0;
}
!
then
   :
else
   msg 'ERROR: we require either the setenv(3) or putenv(3) functions.'
   config_exit 1
fi

if link_check termios 'termios.h and tc*(3) family' << \!
#include <termios.h>
int main(void){
   struct termios tios;

   tcgetattr(0, &tios);
   tcsetattr(0, TCSANOW | TCSADRAIN | TCSAFLUSH, &tios);
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

## optional stuff

if link_check vsnprintf 'vsnprintf(3)' << \!
#include <stdarg.h>
#include <stdio.h>
static void dome(char *buf, size_t blen, ...){
   va_list ap;

   va_start(ap, buf);
   vsnprintf(buf, blen, "%s", ap);
   va_end(ap);
}
int main(void){
   char b[20];

   dome(b, sizeof b, "string");
   return 0;
}
!
then
   :
else
   feat_bail_required ERRORS
fi

if [ "${have_vsnprintf}" = yes ]; then
   link_check va_copy 'va_copy(3)' '#define HAVE_VA_COPY' << \!
#include <stdarg.h>
#include <stdio.h>
static void dome2(char *buf, size_t blen, va_list src){
   va_list ap;

   va_copy(ap, src);
   vsnprintf(buf, blen, "%s", ap);
   va_end(ap);
}
static void dome(char *buf, size_t blen, ...){
   va_list ap;

   va_start(ap, buf);
   dome2(buf, blen, ap);
   va_end(ap);
}
int main(void){
   char b[20];

   dome(b, sizeof b, "string");
   return 0;
}
!
fi

run_check pathconf 'f?pathconf(2)' '#define HAVE_PATHCONF' << \!
#include <unistd.h>
#include <errno.h>
int main(void){
   int rv = 0;

   errno = 0;
   rv |= !(pathconf(".", _PC_NAME_MAX) >= 0 || errno == 0 || errno != ENOSYS);
   errno = 0;
   rv |= !(pathconf(".", _PC_PATH_MAX) >= 0 || errno == 0 || errno != ENOSYS);

   /* Only link check */
   fpathconf(0, _PC_NAME_MAX);

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

if run_check realpath 'realpath(3)' '#define HAVE_REALPATH' << \!
#include <stdlib.h>
int main(void){
   char x_buf[4096], *x = realpath(".", x_buf);

   return (x != NULL) ? 0 : 1;
}
!
then
   if run_check realpath_malloc 'realpath(3) takes NULL' \
         '#define HAVE_REALPATH_NULL' << \!
#include <stdlib.h>
int main(void){
   char *x = realpath(".", NULL);

   if(x != NULL)
      free(x);
   return (x != NULL) ? 0 : 1;
}
!
   then
      :
   fi
fi

## optional and selectable

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

## Now it is the time to fork away the BASE_ series

${rm} -f ${tmp}
squeeze_em ${inc} ${tmp}
${mv} ${tmp} ${inc}
squeeze_em ${lib} ${tmp}
${mv} ${tmp} ${lib}

echo "BASE_LIBS = `${cat} ${lib}`" >> ${mk}
echo "BASE_INCS = `${cat} ${inc}`" >> ${mk}

## The remains are expected to be used only by the main MUA binary!

OPT_LOCALES=0
link_check setlocale 'setlocale(3)' '#define HAVE_SETLOCALE' << \!
#include <locale.h>
int main(void){
   setlocale(LC_ALL, "");
   return 0;
}
!
[ -n "${have_setlocale}" ] && OPT_LOCALES=1

OPT_MBYTE_CHARSETS=0
OPT_WIDE_GLYPHS=0
OPT_TERMINAL_CHARSET=0
if [ -n "${have_setlocale}" ]; then
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
   [ -n "${have_c90amend1}" ] && OPT_MBYTE_CHARSETS=1

   if [ -n "${have_c90amend1}" ]; then
      link_check wcwidth 'wcwidth(3)' '#define HAVE_WCWIDTH' << \!
#include <wchar.h>
int main(void){
   wcwidth(L'c');
   return 0;
}
!
      [ -n "${have_wcwidth}" ] && OPT_WIDE_GLYPHS=1
   fi

   link_check nl_langinfo 'nl_langinfo(3)' '#define HAVE_NL_LANGINFO' << \!
#include <langinfo.h>
#include <stdlib.h>
int main(void){
   nl_langinfo(DAY_1);
   return (nl_langinfo(CODESET) == NULL);
}
!
   [ -n "${have_nl_langinfo}" ] && OPT_TERMINAL_CHARSET=1
fi # have_setlocale

link_check fnmatch 'fnmatch(3)' '#define HAVE_FNMATCH' << \!
#include <fnmatch.h>
int main(void){
   return (fnmatch("*", ".", FNM_PATHNAME | FNM_PERIOD) == FNM_NOMATCH);
}
!

link_check dirent_d_type 'struct dirent.d_type' '#define HAVE_DIRENT_TYPE' << \!
#include <dirent.h>
int main(void){
   struct dirent de;
   return !(de.d_type == DT_UNKNOWN ||
      de.d_type == DT_DIR || de.d_type == DT_LNK);
}
!

## optional and selectable

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
   echo '/* OPT_ICONV=0 */' >> ${h}
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
   echo '/* OPT_SOCKETS=0 */' >> ${h}
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

      link_check rand_egd 'OpenSSL RAND_egd(3ssl)' \
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
   echo '/* OPT_SSL=0 */' >> ${h}
fi # feat_yes SSL

if [ "${have_openssl}" = 'yes' ]; then
   OPT_SMIME=1
else
   OPT_SMIME=1
fi

if feat_yes SMTP; then
   echo '#define HAVE_SMTP' >> ${h}
else
   echo '/* OPT_SMTP=0 */' >> ${h}
fi

if feat_yes POP3; then
   echo '#define HAVE_POP3' >> ${h}
else
   echo '/* OPT_POP3=0 */' >> ${h}
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
   echo '/* OPT_GSSAPI=0 */' >> ${h}
fi # feat_yes GSSAPI

if feat_yes NETRC; then
   echo '#define HAVE_NETRC' >> ${h}
else
   echo '/* OPT_NETRC=0 */' >> ${h}
fi

if feat_yes AGENT; then
   echo '#define HAVE_AGENT' >> ${h}
else
   echo '/* OPT_AGENT=0 */' >> ${h}
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
   echo '/* OPT_IDNA=0 */' >> ${h}
fi

if feat_yes IMAP_SEARCH; then
   echo '#define HAVE_IMAP_SEARCH' >> ${h}
else
   echo '/* OPT_IMAP_SEARCH=0 */' >> ${h}
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
   echo '/* OPT_REGEX=0 */' >> ${h}
fi

if feat_yes MLE && [ -n "${have_c90amend1}" ]; then
   have_mle=1
   echo '#define HAVE_MLE' >> ${h}
else
   feat_bail_required MLE
   echo '/* OPT_MLE=0 */' >> ${h}
fi

# Generic have-a-line-editor switch for those who need it below
if [ -n "${have_mle}" ]; then
   have_cle=1
fi

if [ -n "${have_cle}" ] && feat_yes HISTORY; then
   echo '#define HAVE_HISTORY' >> ${h}
else
   echo '/* OPT_HISTORY=0 */' >> ${h}
fi

if [ -n "${have_mle}" ] && feat_yes KEY_BINDINGS; then
   echo '#define HAVE_KEY_BINDINGS' >> ${h}
else
   echo '/* OPT_KEY_BINDINGS=0 */' >> ${h}
fi

if feat_yes TERMCAP; then
   __termcaplib() {
      link_check termcap "termcap(5) (via ${4})" \
         "#define HAVE_TERMCAP${3}" "${1}" << _EOT
#include <stdio.h>
#include <stdlib.h>
${2}
#include <term.h>
#define UNCONST(P) ((void*)(unsigned long)(void const*)(P))
static int my_putc(int c){return putchar(c);}
int main(void){
   char buf[1024+512], cmdbuf[2048], *cpb, *r1;
   int r2 = OK, r3 = ERR;

   tgetent(buf, getenv("TERM"));
   cpb = cmdbuf;
   r1 = tgetstr(UNCONST("cm"), &cpb);
   tgoto(r1, 1, 1);
   r2 = tgetnum(UNCONST("Co"));
   r3 = tgetflag(UNCONST("ut"));
   tputs("cr", 1, &my_putc);
   return (r1 == NULL || r2 == -1 || r3 == 0);
}
_EOT
   }

   __terminfolib() {
      link_check terminfo "terminfo(5) (via ${2})" \
         '#define HAVE_TERMCAP
         #define HAVE_TERMCAP_CURSES
         #define HAVE_TERMINFO' "${1}" << _EOT
#include <stdio.h>
#include <curses.h>
#include <term.h>
#define UNCONST(P) ((void*)(unsigned long)(void const*)(P))
static int my_putc(int c){return putchar(c);}
int main(void){
   int er, r0, r1, r2;
   char *r3, *tp;

   er = OK;
   r0 = setupterm(NULL, 1, &er);
   r1 = tigetflag(UNCONST("bce"));
   r2 = tigetnum(UNCONST("colors"));
   r3 = tigetstr(UNCONST("cr"));
   tp = tparm(r3, NULL, NULL, 0,0,0,0,0,0,0);
   tputs(tp, 1, &my_putc);
   return (r0 == ERR || r1 == -1 || r2 == -2 || r2 == -1 ||
      r3 == (char*)-1 || r3 == NULL);
}
_EOT
   }

   if feat_yes TERMCAP_VIA_TERMINFO; then
      __terminfolib -ltinfo -ltinfo ||
         __terminfolib -lcurses -lcurses ||
         __terminfolib -lcursesw -lcursesw ||
         feat_bail_required TERMCAP_VIA_TERMINFO
   fi

   if [ -z "${have_terminfo}" ]; then
      __termcaplib -ltermcap '' '' '-ltermcap' ||
         __termcaplib -ltermcap '#include <curses.h>' '
            #define HAVE_TERMCAP_CURSES' \
            'curses.h / -ltermcap' ||
         __termcaplib -lcurses '#include <curses.h>' '
            #define HAVE_TERMCAP_CURSES' \
            'curses.h / -lcurses' ||
         __termcaplib -lcursesw '#include <curses.h>' '
            #define HAVE_TERMCAP_CURSES' \
            'curses.h / -lcursesw' ||
         feat_bail_required TERMCAP

      if [ -n "${have_termcap}" ]; then
         run_check tgetent_null \
            "tgetent(3) of termcap(5) takes NULL buffer" \
            "#define HAVE_TGETENT_NULL_BUF" << _EOT
#include <stdio.h> /* For C89 NULL */
#include <stdlib.h>
#ifdef HAVE_TERMCAP_CURSES
# include <curses.h>
#endif
#include <term.h>
int main(void){
   tgetent(NULL, getenv("TERM"));
   return 0;
}
_EOT
      fi
   fi
else
   echo '/* OPT_TERMCAP=0 */' >> ${h}
   echo '/* OPT_TERMCAP_VIA_TERMINFO=0 */' >> ${h}
fi

if feat_yes SPAM_SPAMC; then
   echo '#define HAVE_SPAM_SPAMC' >> ${h}
   if command -v spamc >/dev/null 2>&1; then
      echo "#define SPAM_SPAMC_PATH \"`command -v spamc`\"" >> ${h}
   fi
else
   echo '/* OPT_SPAM_SPAMC=0 */' >> ${h}
fi

if feat_yes SPAM_SPAMD && [ -n "${have_af_unix}" ]; then
   echo '#define HAVE_SPAM_SPAMD' >> ${h}
else
   feat_bail_required SPAM_SPAMD
   echo '/* OPT_SPAM_SPAMD=0 */' >> ${h}
fi

feat_def SPAM_FILTER

if feat_yes SPAM_SPAMC || feat_yes SPAM_SPAMD || feat_yes SPAM_FILTER; then
   echo '#define HAVE_SPAM' >> ${h}
else
   echo '/* HAVE_SPAM */' >> ${h}
fi

if feat_yes QUOTE_FOLD &&\
      [ -n "${have_c90amend1}" ] && [ -n "${have_wcwidth}" ]; then
   echo '#define HAVE_QUOTE_FOLD' >> ${h}
else
   feat_bail_required QUOTE_FOLD
   echo '/* OPT_QUOTE_FOLD=0 */' >> ${h}
fi

feat_def FILTER_HTML_TAGSOUP
feat_def COLOUR
feat_def DOTLOCK
feat_def MD5
feat_def NOMEMDBG

## Summarizing

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
printf '\n' >> ${h}

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

# Finally, create the string that is used by *feature* and `version'.
# Take this nice opportunity and generate a visual listing of included and
# non-included features for the person who runs the configuration
msg '\nThe following features are included (+) or not (-):'
set -- ${OPTIONS_DETECT} ${OPTIONS} ${OPTIONS_XTRA}
printf '/* The "feature string" */\n' >> ${h}
# Because + is expanded by *folder* if first in "echo $features", put something
printf '#define VAL_FEATURES_CNT '${#}'\n#define VAL_FEATURES "#' >> ${h}
sep=
for opt
do
   sopt="`echo ${opt} | ${tr} '[A-Z]_' '[a-z]-'`"
   feat_yes ${opt} && sign=+ || sign=-
   printf -- "${sep}${sign}${sopt}" >> ${h}
   sep=','
   i=`option_doc_of ${opt}`
   [ -z "${i}" ] && continue
   msg " %s %s: %s" ${sign} ${sopt} "${i}"
done
# TODO instead of using sh+tr+awk+printf, use awk, drop option_doc_of, inc here
#exec 5>&1 >>${h}
#${awk} -v opts="${OPTIONS_DETECT} ${OPTIONS} ${OPTIONS_XTRA}" \
#   -v xopts="${XOPTIONS_DETECT} ${XOPTIONS} ${XOPTIONS_XTRA}" \
printf '"\n#endif /* n_CONFIG_H */\n' >> ${h}

echo "LIBS = `${cat} ${lib}`" >> ${mk}
echo "INCS = `${cat} ${inc}`" >> ${mk}
echo >> ${mk}
${cat} ./mk-mk.in >> ${mk}

## Finished!

msg '\nSetup:'
msg ' . System-wide resource file: %s/%s' "${VAL_SYSCONFDIR}" "${VAL_SYSCONFRC}"
msg ' . bindir: %s' "${VAL_BINDIR}"
if feat_yes DOTLOCK; then
   msg ' . libexecdir: %s' "${VAL_LIBEXECDIR}"
fi
msg ' . mandir: %s' "${VAL_MANDIR}"
msg ' . M(ail)T(ransfer)A(gent): %s (argv0 %s)' "${VAL_MTA}" "${VAL_MTA_ARGV0}"
msg ' . $MAIL spool directory: %s' "${VAL_MAIL}"
msg ''

if [ -n "${have_fnmatch}" ] && [ -n "${have_fchdir}" ]; then
   exit 0
fi
msg 'Remarks:'
if [ -z "${have_fnmatch}" ]; then
   msg ' . The function fnmatch(3) could not be found.'
   msg '   Filename patterns like wildcard are not supported on your system'
fi
if [ -z "${have_fchdir}" ]; then
   msg ' . The function fchdir(2) could not be found.'
   msg '   We will use chdir(2) instead.'
   msg '   This is a problem only if the current working directory is changed'
   msg '   while this program is inside of it'
fi
msg ''

# s-it-mode
