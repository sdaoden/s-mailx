#!/bin/sh -
#@ Please see INSTALL and make.rc instead.

LC_ALL=C
export LC_ALL

# For heaven's sake auto-redirect on SunOS/Solaris
if [ -z "${__MAKE_CONFIG_UP}" ] && [ -d /usr/xpg4 ]; then
   __MAKE_CONFIG_UP=y
   PATH=/usr/xpg4/bin:${PATH}
   export __MAKE_CONFIG_UP PATH

   if [ "x${SHELL}" = x ] || [ "${SHELL}" = /bin/sh ]; then
      SHELL=/usr/xpg4/bin/sh
      export SHELL
      echo >&2 'SunOS/Solaris, redirecting through $SHELL=/usr/xpg4/bin/sh'
      exec /usr/xpg4/bin/sh "${0}" "${@}"
   fi
fi

if [ -z "${SHELL}" ]; then
   SHELL=/bin/sh
   export SHELL
fi

# The feature set, to be kept in sync with make.rc
# If no documentation given, the option is used as such; if doc is '-',
# entry is suppressed when configuration overview is printed, and also in the
# *features* string: most likely for obsolete features etc.
XOPTIONS="\
   CMD_CSOP='csop command: C-style string operations' \
   CMD_FOP='fop command: file and path operations' \
   CMD_VEXPR='vexpr command: evaluate arguments as expressions' \
   COLOUR='Coloured message display' \
   DOCSTRINGS='Command documentation help strings' \
   DOTLOCK='Dotlock files and privilege-separated dotlock program' \
   ERRORS='Log message ring' \
   FILTER_HTML_TAGSOUP='Simple built-in HTML-to-text display filter' \
   FILTER_QUOTE_FOLD='Extended *quote-fold*ing filter' \
   ICONV='Character set conversion using iconv(3)' \
   IDNA='Internationalized Domain Names for Applications (encode only)' \
   IMAP_SEARCH='IMAP-style search expressions' \
   MAILCAP='MIME type handlers defined via mailcap file(s)' \
   MAILDIR='Maildir E-mail directories' \
   MLE='Mailx Line Editor' \
      HISTORY='Line editor history management' \
      KEY_BINDINGS='Configurable key bindings' \
      TERMCAP='Terminal capability queries (termcap(5))' \
         TERMCAP_VIA_TERMINFO='Terminal capability queries use terminfo(5)' \
   MTA_ALIASES='MTA aliases(5) (text file) support' \
   REGEX='Regular expressions' \
   NET='Network support' \
      GSSAPI='Generic Security Service authentication' \
      IMAP='IMAP v4r1 client' \
      MD5='MD5 message digest (APOP, CRAM-MD5)' \
      NETRC='.netrc file support' \
      NET_TEST='-' \
      POP3='Post Office Protocol Version 3 client' \
      SMTP='Simple Mail Transfer Protocol client' \
      TLS='Transport Layer Security (OpenSSL / LibreSSL)' \
         TLS_ALL_ALGORITHMS='Support of all digest and cipher algorithms' \
   SPAM_FILTER='Freely configurable *spam-filter-..*s' \
   SPAM_SPAMC='Spam management via spamc(1) of spamassassin(1)' \
   UISTRINGS='User interface and error message strings' \
"

# Options which are automatically deduced from host environment, i.e., these
# need special treatment all around here to warp from/to OPT_ stuff
# setlocale, C90AMEND1, NL_LANGINFO, wcwidth
XOPTIONS_DETECT="\
   LOCALES='Locale support - printable characters etc. depend on environment' \
   MULTIBYTE_CHARSETS='Multibyte character sets' \
   TERMINAL_CHARSET='Automatic detection of terminal character set' \
   WIDE_GLYPHS='Wide glyph support' \
   FLOCK='flock(2) file locking' \
"

# Rather special options, for custom building, or which always exist.
# Mostly for generating the visual overview and the *features* string
XOPTIONS_XTRA="\
   CROSS_BUILD='Cross-compilation: trust any detected environment' \
   DEBUG='Debug enabled binary, not for end-users: THANKS!' \
   DEVEL='Computers do not blunder' \
   MIME='Multipurpose Internet Mail Extensions' \
   SMIME='S/MIME message signing, verification, en- and decryption' \
"

# To avoid too many recompilations we use a two-stage "configuration changed"
# detection, the first uses mk-config.env, which only goes for actual user
# config settings etc. the second uses mk-config.h, which thus includes the
# things we have truly detected.  This does not work well for multiple choice
# values of which only one will be really used, so those user wishes may not be
# placed in the header, only the really detected one (but that has to!).
# Used for grep(1), for portability assume fixed matching only.
H_BLACKLIST='-e VAL_ICONV -e VAL_IDNA -e VAL_RANDOM'

# The problem is that we don't have any tools we can use right now, so
# encapsulate stuff in functions which get called in right order later on

option_reset() {
   set -- ${OPTIONS}
   for i
   do
      eval j=\$OPT_${i}
      [ -n "${j}" ] && eval SAVE_OPT_${i}=${j}
      eval OPT_${i}=0
   done
}

option_maximal() {
   set -- ${OPTIONS}
   for i
   do
      eval OPT_${i}=1
   done
   OPT_DOTLOCK=require OPT_ICONV=require OPT_REGEX=require
}

option_restore() {
   any=
   set -- ${OPTIONS}
   for i
   do
      eval j=\$SAVE_OPT_${i}
      if [ -n "${j}" ]; then
         msg_nonl "${any}${i}=${j}"
         any=', '
         eval OPT_${i}=${j}
      fi
   done
   [ -n "${any}" ] && msg_nonl ' ... '
}

option_setup() {
   option_parse OPTIONS_DETECT "${XOPTIONS_DETECT}"
   option_parse OPTIONS "${XOPTIONS}"
   option_parse OPTIONS_XTRA "${XOPTIONS_XTRA}"
   OPT_MIME=1

   # Predefined CONFIG= urations take precedence over anything else
   if [ -n "${CONFIG}" ]; then
      option_reset
      case "${CONFIG}" in
      [nN][uU][lL][lL])
         ;;
      [nN][uU][lL][lL][iI])
         OPT_ICONV=require
         OPT_UISTRINGS=1
         ;;
      [mM][iI][nN][iI][mM][aA][lL])
         OPT_CMD_CSOP=1
            OPT_CMD_FOP=1
            OPT_CMD_VEXPR=1
         OPT_COLOUR=1
         OPT_DOCSTRINGS=1
         OPT_DOTLOCK=require OPT_ICONV=require OPT_REGEX=require
         OPT_ERRORS=1
         OPT_IDNA=1
         OPT_MAILDIR=1
         OPT_MLE=1
            OPT_HISTORY=1 OPT_KEY_BINDINGS=1
         OPT_SPAM_FILTER=1
         OPT_UISTRINGS=1
         ;;
      [nN][eE][tT][sS][eE][nN][dD])
         OPT_CMD_CSOP=1
            OPT_CMD_FOP=1
            OPT_CMD_VEXPR=1
         OPT_COLOUR=1
         OPT_DOCSTRINGS=1
         OPT_DOTLOCK=require OPT_ICONV=require OPT_REGEX=require
         OPT_ERRORS=1
         OPT_IDNA=1
         OPT_MAILDIR=1
         OPT_MLE=1
            OPT_HISTORY=1 OPT_KEY_BINDINGS=1
         OPT_MTA_ALIASES=1
         OPT_NET=require
            OPT_GSSAPI=1
            OPT_NETRC=1
            OPT_NET_TEST=1
            OPT_SMTP=require
            OPT_TLS=require
         OPT_SPAM_FILTER=1
         OPT_UISTRINGS=1
         ;;
      [mM][aA][xX][iI][mM][aA][lL])
         option_maximal
         ;;
      [dD][eE][vV][eE][lL])
         option_maximal
         OPT_DEVEL=1 OPT_DEBUG=1
         ;;
      [oO][dD][eE][vV][eE][lL])
         option_maximal
         OPT_DEVEL=1
         ;;
      *)
         msg 'failed'
         msg 'ERROR: unknown CONFIG= setting: '${CONFIG}
         msg '   Available are NULL, NULLI, MINIMAL, NETSEND, MAXIMAL'
         exit 1
         ;;
      esac
      msg_nonl "CONFIG=${CONFIG} ... "
      option_restore
   fi
}

# Inter-relationships XXX sort this!
option_update() {
   # 0=pre option evaluate, 1=post, 2=final call, before summary
   case ${1} in
   0)
      ;;
   1)
      if feat_yes DEBUG || feat_yes DEVEL; then
         val_strip VAL_AUTOCC keep_options
      fi
      ;;
   2)
   esac

   if feat_no TLS; then
      OPT_TLS_ALL_ALGORITHMS=0
   fi

   if feat_no SMTP && feat_no POP3 && feat_no IMAP; then
      OPT_NET=0
   fi
   if feat_no NET; then
      if feat_require IMAP; then
         msg 'ERROR: need NETwork for required feature IMAP'
         config_exit 13
      fi
      if feat_require POP3; then
         msg 'ERROR: need NETwork for required feature POP3'
         config_exit 13
      fi
      if feat_require SMTP; then
         msg 'ERROR: need NETwork for required feature SMTP'
         config_exit 13
      fi
      OPT_GSSAPI=0
      OPT_IMAP=0
      OPT_MD5=0
      OPT_NETRC=0
      OPT_NET_TEST=0
      OPT_POP3=0
      OPT_SMTP=0
      OPT_TLS=0 OPT_TLS_ALL_ALGORITHMS=0
   fi
   if feat_no SMTP && feat_no IMAP; then
      OPT_GSSAPI=0
   fi

   if feat_no ICONV; then
      if feat_yes IMAP; then
         if feat_yes ALWAYS_UNICODE_LOCALE; then
            msg 'WARN: no ICONV, keeping IMAP due to ALWAYS_UNICODE_LOCALE!'
         elif feat_require IMAP; then
            msg 'ERROR: need ICONV for required feature IMAP'
            config_exit 13
         else
            msg 'ERROR: disabling IMAP due to missing ICONV'
            OPT_IMAP=0
         fi
      fi

      if feat_yes IDNA; then
         if feat_require IDNA; then
            msg 'ERROR: need ICONV for required feature IDNA'
            config_exit 13
         fi
         msg 'ERROR: disabling IDNA due to missing ICONV'
         OPT_IDNA=0
      fi
   fi

   if feat_no MLE; then
      OPT_HISTORY=0 OPT_KEY_BINDINGS=0
      OPT_TERMCAP=0 OPT_TERMCAP_VIA_TERMINFO=0
   elif feat_no TERMCAP; then
      OPT_TERMCAP_VIA_TERMINFO=0
   fi
}

##  --  >8  - << OPTIONS | EARLY >> -  8<  --  ##

# Note that potential duplicates in PATH, C_INCLUDE_PATH etc. will be cleaned
# via path_check() later on once possible

COMMLINE="${*}"

# which(1) not standardized, command(1) -v may return non-executable: unroll!
SU_FIND_COMMAND_INCLUSION=1 . "${TOPDIR}"mk/su-find-command.sh
# Also not standardized: a way to round-trip quote
. "${TOPDIR}"mk/su-quote-rndtrip.sh

##  --  >8  - << EARLY | OS/CC >> -  8<  --  ##

# TODO cc_maxopt is brute simple, we should compile test program and dig real
# compiler versions for known compilers, then be more specific
[ -n "${cc_maxopt}" ] || cc_maxopt=100
cc_os_search=
cc_no_stackprot=
cc_no_fortify=
cc_no_flagtest=
ld_need_R_flags=
ld_no_bind_now=
ld_rpath_not_runpath=

_CFLAGS= _LDFLAGS=

os_early_setup() {
   # We don't "have any utility" (see make.rc)
   [ -n "${OS}" ] && [ -n "${OSFULLSPEC}" ] ||
      thecmd_testandset_fail uname uname

   [ -n "${OS}" ] || OS=`${uname} -s`
   export OS
   msg 'Operating system is %s' "${OS}"

   if [ ${OS} = SunOS ]; then
      # According to standards(5), this is what we need to do
      if [ -d /usr/xpg4 ]; then :; else
         msg 'ERROR: On SunOS / Solaris we need /usr/xpg4 environment!  Sorry.'
         config_exit 1
      fi
      # xpg4/bin was already added at top, but we need it first and it will be
      # cleaned up via path_check along the way
      PATH="/usr/xpg4/bin:/usr/ccs/bin:/usr/bin:${PATH}"
      [ -d /usr/xpg6 ] && PATH="/usr/xpg6/bin:${PATH}"
      export PATH
   fi
}

os_setup() {
   # OSFULLSPEC is used to recognize changes (i.e., machine type, updates
   # etc.), it is not baked into the binary
   [ -n "${OSFULLSPEC}" ] || OSFULLSPEC=`${uname} -a`

   if [ ${OS} = darwin ]; then
      msg ' . have special Darwin environmental addons...'
      LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${DYLD_LIBRARY_PATH}
   elif [ ${OS} = sunos ]; then
      msg ' . have special SunOS / Solaris "setup" rules ...'
      cc_os_search=_cc_os_sunos
      _os_setup_sunos
   elif [ ${OS} = unixware ]; then
      cc_os_search=_cc_os_unixware
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
   if feat_yes USE_PKGSYS; then
      if [ -d /usr/pkg ]; then
         msg ' . found pkgsrc(7), merging C_INCLUDE_PATH and LD_LIBRARY_PATH'
         C_INCLUDE_PATH=/usr/pkg/include:${C_INCLUDE_PATH}
         LD_LIBRARY_PATH=/usr/pkg/lib:${LD_LIBRARY_PATH}
         ld_rpath_not_runpath=1
      fi
   fi
}

_os_setup_sunos() {
   C_INCLUDE_PATH=/usr/xpg4/include:${C_INCLUDE_PATH}
   LD_LIBRARY_PATH=/usr/xpg4/lib:${LD_LIBRARY_PATH}

   # Include packages
   if feat_yes USE_PKGSYS; then
      if [ -d /opt/csw ]; then
         msg ' . found OpenCSW PKGSYS'
         C_INCLUDE_PATH=/opt/csw/include:${C_INCLUDE_PATH}
         LD_LIBRARY_PATH=/opt/csw/lib:${LD_LIBRARY_PATH}
         ld_no_bind_now=1 ld_rpath_not_runpath=1
      fi
      if [ -d /opt/schily ]; then
         msg ' . found Schily PKGSYS'
         C_INCLUDE_PATH=/opt/schily/include:${C_INCLUDE_PATH}
         LD_LIBRARY_PATH=/opt/schily/lib:${LD_LIBRARY_PATH}
         ld_no_bind_now=1 ld_rpath_not_runpath=1
      fi
   fi

   OS_DEFINES="${OS_DEFINES}#define __EXTENSIONS__\n"
   #OS_DEFINES="${OS_DEFINES}#define _POSIX_C_SOURCE 200112L\n"

   msg 'Whatever $CC, turning off stack protection (see INSTALL)!'
   cc_maxopt=2 cc_no_stackprot=1
   return 1
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
      export CC EXTRA_CFLAGS EXTRA_LDFLAGS
      return
   else
      val_has VAL_AUTOCC keep_options || CFLAGS= LDFLAGS=
      export CFLAGS LDFLAGS
   fi

   if [ -n "${CC}" ]; then
      _cc_default
      export CC
      return
   fi

   if [ -n "${cc_os_search}" ] && ${cc_os_search}; then
      cc_no_flagtest=1
   else
      msg_nonl 'Searching for a usable C compiler .. $CC='
      if acmd_set CC clang || acmd_set CC gcc ||
            acmd_set CC tcc || acmd_set CC pcc ||
            acmd_set CC c89 || acmd_set CC c99; then
         case "${CC}" in
         *pcc*) cc_no_fortify=1;;
         *) ;;
         esac
      else
         msg 'boing booom tschak'
         msg 'ERROR: I cannot find a compiler!'
         msg ' Neither of clang(1), gcc(1), tcc(1), pcc(1), c89(1) and c99(1).'
         msg ' Please set ${CC} environment variable, maybe ${CFLAGS}, rerun.'
         config_exit 1
      fi
      msg '%s' "${CC}"
   fi
   export CC
}

_cc_os_unixware() {
   if feat_yes AUTOCC && acmd_set CC cc; then
      msg_nonl ' . have special UnixWare rules for $CC ...'
      feat_yes DEBUG && _CFLAGS='-v -Xa -g' || _CFLAGS='-Xa -O'

      CFLAGS="${CFLAGS} ${_CFLAGS} ${EXTRA_CFLAGS}"
      LDFLAGS="${LDFLAGS} ${_LDFLAGS} ${EXTRA_LDFLAGS}"
      export CC CFLAGS LDFLAGS
      OPT_AUTOCC=0 ld_need_R_flags=-R
      msg '%s' "${CC}"
      return 0
   fi
   return 1
}

_cc_os_sunos() {
   if feat_yes AUTOCC && acmd_set CC cc && "${CC}" -flags >/dev/null 2>&1; then
      msg_nonl ' . have special SunOS rules for $CC ...'
      feat_yes DEBUG && _CFLAGS="-v -Xa -g" || _CFLAGS="-Xa -O"

      CFLAGS="${CFLAGS} ${_CFLAGS} ${EXTRA_CFLAGS}"
      LDFLAGS="${LDFLAGS} ${_LDFLAGS} ${EXTRA_LDFLAGS}"
      export CC CFLAGS LDFLAGS
      OPT_AUTOCC=0 ld_need_R_flags=-R
      msg '%s' "${CC}"
      return 0
   else
      CC=
      : # cc_maxopt=2 cc_no_stackprot=1
   fi
   return 1
}

_cc_default() {
   if [ -z "${CC}" ]; then
      msg 'To go on like you have chosen, please set $CC, rerun.'
      config_exit 1
   fi

   if [ -z "${VERBOSE}" ] && [ -f ${env} ] && feat_no DEBUG; then
      :
   else
      msg '. C compiler ${CC}=%s' "${CC}"
   fi

   case "${CC}" in
   *pcc*) cc_no_fortify=1;;
   *) ;;
   esac
}

cc_create_testfile() {
   ${cat} > ${tmp}.c <<\!
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
}

cc_hello() {
   [ -n "${cc_check_silent}" ] || msg_nonl ' . Compiles "Hello world" .. '
   if ${CC} ${INCS} ${CFLAGS} ${EXTRA_CFLAGS} ${LDFLAGS} ${EXTRA_LDFLAGS} \
         -o ${tmp2} ${tmp}.c ${LIBS}; then
      [ -n "${cc_check_silent}" ] || msg 'yes'
      feat_yes CROSS_BUILD && return 0
      [ -n "${cc_check_silent}" ] || msg_nonl ' . Compiled program works .. '
      if ( [ "`\"${tmp2}\"`" = 'Hello world' ] ) >/dev/null 2>&1; then
         [ -n "${cc_check_silent}" ] || msg 'yes'
         return 0
      fi
   fi
   [ -n "${cc_check_silent}" ] || msg 'no'
   msg 'ERROR: i cannot compile or run a "Hello world" via'
   msg '   %s' \
  "${CC} ${INCS} ${CFLAGS} ${EXTRA_CFLAGS} ${LDFLAGS} ${EXTRA_LDFLAGS} ${LIBS}"
   msg 'ERROR:   Please read INSTALL, rerun'
   config_exit 1
}

cc_flags() {
   if [ -n "${cc_no_flagtest}" ]; then
      ld_runtime_flags # update!
   elif feat_yes AUTOCC; then
      if [ -f ${env} ] && feat_no DEBUG && [ -z "${VERBOSE}" ]; then
         cc_check_silent=1
         if [ -f ${env} ]; then :; else
            msg 'Checking ${CFLAGS}/${LDFLAGS} for ${CC}=%s ..' "${CC}"
         fi
      else
         cc_check_silent=
         msg 'Checking ${CFLAGS}/${LDFLAGS} for ${CC}=%s' "${CC}"
      fi

      i=`echo "${CC}" | ${awk} 'BEGIN{FS="/"}{print $NF}'`
      if { echo "${i}" | ${grep} tcc; } >/dev/null 2>&1; then
         msg ' . have special tcc(1) environmental rules ...'
         _cc_flags_tcc
      else
         # As of pcc CVS 2016-04-02, stack protection support is announced but
         # will break if used on Linux
         #if { echo "${i}" | ${grep} pcc; } >/dev/null 2>&1; then
         #   cc_no_stackprot=1
         #fi
         _cc_flags_generic
      fi

      feat_no DEBUG && feat_no DEVEL && _CFLAGS="-DNDEBUG ${_CFLAGS}"
      CFLAGS="${CFLAGS} ${_CFLAGS} ${EXTRA_CFLAGS}"
      LDFLAGS="${LDFLAGS} ${_LDFLAGS} ${EXTRA_LDFLAGS}"
   else
      if feat_no DEBUG && feat_no DEVEL; then
         CFLAGS="-DNDEBUG ${CFLAGS}"
      fi
   fi
   export CFLAGS LDFLAGS
}

_cc_flags_tcc() {
   __cflags=${_CFLAGS} __ldflags=${_LDFLAGS}
   _CFLAGS= _LDFLAGS=

   cc_check -W
   cc_check -Wall
   cc_check -Wextra
   cc_check -pedantic

   if feat_yes DEBUG; then
      # May have problems to find libtcc cc_check -b
      cc_check -g
   fi

   if ld_check -Wl,-rpath =./ no; then
      ld_need_R_flags=-Wl,-rpath=
      if [ -z "${ld_rpath_not_runpath}" ]; then
         ld_check -Wl,--enable-new-dtags
      else
         msg ' ! $LD_LIBRARY_PATH adjusted, not trying --enable-new-dtags'
      fi
      ld_runtime_flags # update!
   fi

   _CFLAGS="${_CFLAGS} ${__cflags}" _LDFLAGS="${_LDFLAGS} ${__ldflags}"
   unset __cflags __ldflags
}

_cc_flags_generic() {
   __cflags=${_CFLAGS} __ldflags=${_LDFLAGS}
   _CFLAGS= _LDFLAGS=
   if feat_yes DEVEL; then
      __w=c89 __x=c99 __y=c11  __z=c18
      __v=$(date +%j)
      if [ -n "${good_shell}" ]; then
         __v=${__v##*0}
      else
         __v=$(echo ${__v} | ${sed} -e 's/^0*//')
      fi
      if [ ${?} -eq 0 ]; then
         case "$((__v % 4))" in
         0) __w=c89 __x=c99 __y=c11 __z=c18;;
         1) __w=c99 __x=c11 __y=c18 __z=c89;;
         2) __w=c11 __x=c18 __y=c89 __z=c99;;
         *) __w=c18 __x=c89 __y=c99 __z=c11;;
         esac
      fi
      msg ' # DEVEL, std checks: '${__w}', '${__x}', '${__y}', '${__z}
      cc_check -std=${__w} || cc_check -std=${__x} || cc_check -std=${__y} ||
         cc_check -std=${__z}
      unset __w __x __y __z
   else
      cc_check -std=c99
   fi

   # E.g., valgrind does not work well with high optimization
   if [ ${cc_maxopt} -gt 1 ] && feat_yes NOMEMDBG &&
         feat_no ASAN_ADDRESS && feat_no ASAN_MEMORY; then
      msg ' ! OPT_NOMEMDBG, setting cc_maxopt=1 (-O1)'
      cc_maxopt=1
   fi
   # Check -g first since some others may rely upon -g / optim. level
   if feat_yes DEBUG; then
      cc_check -O
      cc_check -g
   elif val_has VAL_AUTOCC keep_options; then
      :
   elif [ ${cc_maxopt} -gt 2 ] && cc_check -O3; then
      :
   elif [ ${cc_maxopt} -gt 1 ] && cc_check -O2; then
      :
   elif [ ${cc_maxopt} -gt 0 ] && cc_check -O1; then
      :
   else
      cc_check -O
   fi

   if feat_yes AMALGAMATION; then
      cc_check -pipe
   fi

   #if feat_yes DEVEL && cc_check -Weverything; then
   #   :
   #else
      cc_check -W
      cc_check -Wall
      cc_check -Wextra
      if feat_yes DEVEL; then
         cc_check -Wbad-function-cast
         cc_check -Wcast-align
         cc_check -Wcast-qual
         cc_check -Wformat-security # -Wformat via -Wall
            cc_check -Wformat-signedness
         cc_check -Winit-self
         cc_check -Wmissing-prototypes
         cc_check -Wshadow
         cc_check -Wunused
         cc_check -Wwrite-strings
         cc_check -Wno-long-long
      fi
   #fi
   cc_check -pedantic
   if feat_no DEVEL; then
      if feat_yes AMALGAMATION; then
         cc_check -Wno-unused-function
      fi
      if cc_check -Wno-uninitialized; then :; else
         cc_check -Wno-maybe-uninitialized
      fi
      cc_check -Wno-unused-result
      cc_check -Wno-unused-value
   fi

   cc_check -fno-asynchronous-unwind-tables
   cc_check -fno-common
   cc_check -fno-unwind-tables
   cc_check -fstrict-aliasing
   if cc_check -fstrict-overflow && feat_yes DEVEL; then # XXX always
      cc_check -Wstrict-overflow=5
   fi

   if val_has VAL_AUTOCC stackprot; then
      if [ -z "${cc_no_stackprot}" ]; then
         if cc_check -fstack-protector-strong ||
               cc_check -fstack-protector-all; then
            if val_has VAL_AUTOCC fortify; then
               if [ -z "${cc_no_fortify}" ]; then
                  cc_check -D_FORTIFY_SOURCE=2
               else
                  msg ' ! No check for -D_FORTIFY_SOURCE=2 ${CC} option,'
                  msg ' ! it caused errors in a "similar" configuration.'
                  msg ' ! You may turn off OPT_AUTOCC, then rerun.'
               fi
            fi
         fi
      else
         msg ' ! No check for -D_FORTIFY_SOURCE=2 ${CC} option,'
         msg ' ! it caused errors in a "similar" configuration.'
         msg ' ! You may turn off OPT_AUTOCC, then rerun.'
      fi
   fi

   # LD (+ dependent CC)

   if feat_yes ASAN_ADDRESS; then
      _ccfg=${_CFLAGS}
      if cc_check -fsanitize=address && ld_check -fsanitize=address; then
         :
      else
         feat_bail_required ASAN_ADDRESS
         _CFLAGS=${_ccfg}
      fi
   fi

   if feat_yes ASAN_MEMORY; then
      _ccfg=${_CFLAGS}
      if cc_check -fsanitize=memory && ld_check -fsanitize=memory &&
            cc_check -fsanitize-memory-track-origins=2 &&
            ld_check -fsanitize-memory-track-origins=2; then
         :
      else
         feat_bail_required ASAN_MEMORY
         _CFLAGS=${_ccfg}
      fi
   fi

   if feat_yes USAN; then
      _ccfg=${_CFLAGS}
      if cc_check -fsanitize=undefined && ld_check -fsanitize=undefined; then
         :
      else
         feat_bail_required USAN
         _CFLAGS=${_ccfg}
      fi
   fi

   ld_check -Wl,-z,relro
   if val_has VAL_AUTOCC bind_now; then
      if [ -z "${ld_no_bind_now}" ]; then
         ld_check -Wl,-z,now
      else
         msg ' ! $LD_LIBRARY_PATH adjusted, not trying -Wl,-z,now'
      fi
   fi
   ld_check -Wl,-z,noexecstack
   ld_check -Wl,--as-needed
   if ld_check -Wl,-rpath =./ no; then
      ld_need_R_flags=-Wl,-rpath=
      # Choose DT_RUNPATH (after $LD_LIBRARY_PATH) over DT_RPATH (before)
      if [ -z "${ld_rpath_not_runpath}" ]; then
         ld_check -Wl,--enable-new-dtags
      else
         msg ' ! $LD_LIBRARY_PATH adjusted, not trying --enable-new-dtags'
      fi
      ld_runtime_flags # update!
   elif ld_check -Wl,-R ./ no; then
      ld_need_R_flags=-Wl,-R
      if [ -z "${ld_rpath_not_runpath}" ]; then
         ld_check -Wl,--enable-new-dtags
      else
         msg ' ! $LD_LIBRARY_PATH adjusted, not trying --enable-new-dtags'
      fi
      ld_runtime_flags # update!
   fi

   # Address randomization
   if val_has VAL_AUTOCC pie; then
      _ccfg=${_CFLAGS}
      if cc_check -fPIE || cc_check -fpie; then
         ld_check -pie || _CFLAGS=${_ccfg}
      fi
      unset _ccfg
   fi

   # Retpoline (xxx maybe later?)
#   _ccfg=${_CFLAGS} _i=
#   if cc_check -mfunction-return=thunk; then
#      if cc_check -mindirect-branch=thunk; then
#         _i=1
#      fi
#   elif cc_check -mretpoline; then
#      _i=1
#   fi
#   if [ -n "${_i}" ]; then
#      ld_check -Wl,-z,retpolineplt || _i=
#   fi
#   [ -n "${_i}" ] || _CFLAGS=${_ccfg}
#   unset _ccfg

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

t1=ten10one1ten10one1
if ( [ ${t1##*ten10} = one1 ] && [ ${t1#*ten10} = one1ten10one1 ] &&
      [ ${t1%%one1*} = ten10 ] && [ ${t1%one1*} = ten10one1ten10 ]
      ) > /dev/null 2>&1; then
   good_shell=1
else
   unset good_shell
fi
unset t1

( set -o noglob ) >/dev/null 2>&1 && noglob_shell=1 || unset noglob_shell

config_exit() {
   exit ${1}
}

# Our feature check environment
_feats_eval_done=0

_feat_val_no() {
   [ "x${1}" = x0 ] || [ "x${1}" = xn ] ||
   [ "x${1}" = xfalse ] || [ "x${1}" = xno ] || [ "x${1}" = xoff ]
}

_feat_val_yes() {
   [ "x${1}" = x1 ] || [ "x${1}" = xy ] ||
   [ "x${1}" = xtrue ] || [ "x${1}" = xyes ] || [ "x${1}" = xon ] ||
         [ "x${1}" = xrequire ]
}

_feat_val_require() {
   [ "x${1}" = xrequire ]
}

_feat_check() {
   eval _fc_i=\$OPT_${1}
   if [ "$_feats_eval_done" = 1 ]; then
      [ "x${_fc_i}" = x0 ] && return 1
      return 0
   fi
   _fc_i="`echo ${_fc_i} | ${tr} '[A-Z]' '[a-z]'`"
   if _feat_val_no "${_fc_i}"; then
      return 1
   elif _feat_val_yes "${_fc_i}"; then
      return 0
   else
      msg "ERROR: %s: 0/n/false/no/off or 1/y/true/yes/on/require, got: %s" \
         "${1}" "${_fc_i}"
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
   eval _fr_i=\$OPT_${1}
   _fr_i="`echo ${_fr_i} | ${tr} '[A-Z]' '[a-z]'`"
   [ "x${_fr_i}" = xrequire ] || [ "x${_fr_i}" = xrequired ]
}

feat_bail_required() {
   if feat_require ${1}; then
      msg 'ERROR: feature OPT_%s is required but not available' "${1}"
      config_exit 13
   fi
   feat_is_unsupported "${1}"
}

feat_is_disabled() {
   [ ${#} -eq 1 ] && msg ' .  (disabled: OPT_%s)' "${1}"
   echo "/* OPT_${1} -> mx_HAVE_${1} */" >> ${h}
}

feat_is_unsupported() {
   msg ' ! NOTICE: unsupported: OPT_%s' "${1}"
   echo "/* OPT_${1} -> mx_HAVE_${1} */" >> ${h}
   eval OPT_${1}=0
}

feat_def() {
   if feat_yes ${1}; then
      [ -n "${VERBOSE}" ] && msg ' . %s ... yes' "${1}"
      echo '#define mx_HAVE_'${1}'' >> ${h}
      return 0
   else
      feat_is_disabled "${@}"
      return 1
   fi
}

option_parse() {
   # Parse one of our XOPTIONS* in $2 and assign the sh(1) compatible list of
   # options, without documentation, to $1
   j=\'
   i="`${awk} -v input=\"${2}\" '
      BEGIN{
         for(i = 0;;){
            voff = match(input, /[0-9a-zA-Z_]+(='${j}'[^'${j}']+)?/)
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
         voff = match(input, /[0-9a-zA-Z_]+(='${j}'[^'${j}']+)?/)
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
   # We want read(1) to perform reverse solidus escaping in order to be able to
   # use multiline values in make.rc; the resulting sh(1)/sed(1) code was very
   # slow in VMs (see [fa2e248]), Aharon Robbins suggested the following
   < ${rc} ${awk} 'BEGIN{line = ""}{
      sub(/^[ 	]+/, "", $0)
      sub(/[ 	]+$/, "", $0)
      if(sub(/\\$/, "", $0)){
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
            sub(/=.*$/, "", LINE)
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
            sub(/^[^=]*=/, "", LINE)
            sub(/^"*/, "", LINE)
            sub(/"*$/, "", LINE)
            # Sun xpg4/bin/awk expands those twice:
            #  Notice that backslash escapes are interpreted twice, once in
            #  lexical processing of the string and once in processing the
            #  regular expression.
               i = "\""
               gsub(/"/, "\\\\\"", i)
               i = (i == "\134\"")
            gsub(/"/, (i ? "\\\\\"" : "\134\""), LINE)
            print LINE
         }'`
      fi
      [ "${i}" = "DESTDIR" ] && continue
      [ "${i}" = "OBJDIR" ] && continue
      echo "${i}=\"${j}\""
   done > ${tmp}
   # Reread the mixed version right now
   . ${tmp}
}

option_evaluate() {
   # Expand the option values, which may contain shell snippets
   # Set booleans to 0 or 1, or require, set _feats_eval_done=1
   ${rm} -f ${newenv} ${newmk}

   exec 7<&0 8>&1 <${tmp} >${newenv}
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
         if [ -z "${j}" ] || _feat_val_no "${j}"; then
            j=0
            printf "   /* #undef ${i} */\n" >> ${newh}
         elif _feat_val_yes "${j}"; then
            if _feat_val_require "${j}"; then
               j=require
            else
               j=1
            fi
            printf "   /* #define ${i} */\n" >> ${newh}
         else
            msg 'ERROR: cannot parse <%s>' "${line}"
            config_exit 1
         fi
      elif { echo ${i} | ${grep} ${H_BLACKLIST} >/dev/null 2>&1; }; then
         :
      else
         printf "#define ${i} \"${j}\"\n" >> ${newh}
      fi
      printf -- "${i} = ${j}\n" >> ${newmk}
      printf -- "${i}=%s;export ${i}\n" "`quote_string ${j}`"
      eval "${i}=\"${j}\""
   done
   exec 0<&7 1>&8 7<&- 8<&-

   _feats_eval_done=1
}

val_allof() {
   eval __expo__=\$${1}
   ${awk} -v HEAP="${2}" -v USER="${__expo__}" '
      BEGIN{
         i = split(HEAP, ha, /[, ]/)
         if((j = split(USER, ua, /[, ]/)) == 0)
            exit
         for(; j != 0; --j){
            us = tolower(ua[j])
            if(us == "all" || us == "any")
               continue
            ok = 0
            for(ii = i; ii != 0; --ii)
               if(tolower(ha[ii]) == us){
                  ok = 1
                  break
               }
            if(!ok)
               exit 1
         }
      }
   '
   __rv__=${?}
   [ ${__rv__} -ne 0 ] && return ${__rv__}

    if ${awk} -v USER="${__expo__}" '
            BEGIN{
               if((j = split(USER, ua, /[, ]/)) == 0)
                  exit
               for(; j != 0; --j){
                  us = tolower(ua[j])
                  if(us == "all" || us == "any")
                     exit 0
               }
               exit 1
            }
         '; then
      eval "${1}"=\"${2}\"
   else
      # Enforce lowercase also in otherwise unchanged user value..
      eval "${1}"=\""`echo ${__expo__} | ${tr} '[A-Z]_' '[a-z]-'`"\"
   fi
   return 0
}

val_foreach_call() {
   eval __expo__=\$${1} __mod__=${2}
   __oifs__=${IFS}
   IFS=", "
   set -- ${__expo__}
   IFS=${__oifs__}
   for __vfec__
   do
      eval ${__mod__}_${__vfec__} && break
   done
}

val_has() {
   eval __expo__=\$${1}
   __expo__=`echo ${__expo__} | tr [[:upper:]] [[:lower:]]`
   __vhasx__=`echo ${2} | tr [[:upper:]] [[:lower:]]`
   __oifs__=${IFS}
   IFS=", "
   set -- ${__expo__}
   IFS=${__oifs__}
   for __vhasy__
   do
      [ "${__vhasx__}" = "${__vhasy__}" ] && return 0
   done
   return 1
}

val_strip() {
   __name__=${1}
   eval __expo__=\$${__name__}
   __expo__=`echo ${__expo__} | tr [[:upper:]] [[:lower:]]`
   __vhasx__=`echo ${2} | tr [[:upper:]] [[:lower:]]`
   __oifs__=${IFS}
   IFS=", "
   set -- ${__expo__}
   IFS=${__oifs__}
   __vnew__=
   for __vhasy__
   do
      [ "${__vhasx__}" = "${__vhasy__}" ] && continue
      [ -n "${__vnew__}" ] && __vnew__=${__vnew__},
      __vnew__=${__vnew__}${__vhasy__}
   done
   eval ${__name__}=${__vnew__}
}

oneslash() {
   </dev/null ${awk} -v X="${1}" '
      BEGIN{
         i = match(X, "/+$")
         if(RSTART != 0)
            X = substr(X, 1, RSTART - 1)
         X = X "/"
         print X
      }
   '
}

path_is_absolute() {
   { echo "${*}" | ${grep} ^/; } >/dev/null 2>&1
   return $?
}

path_check() {
   # "path_check VARNAME" or "path_check VARNAME FLAG VARNAME"
   varname=${1} addflag=${2} flagvarname=${3}
   j=${IFS}
   IFS=:
   [ -n "${noglob_shell}" ] && set -o noglob
   eval "set -- \$${1}"
   [ -n "${noglob_shell}" ] && set +o noglob
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
            # But do not link any fakeroot path into our binaries!
            if [ -n "${addflag}" ]; then
               case "${i}" in *fakeroot*) continue;; esac
               k="${k} ${addflag}${i}"
            fi
         fi
      else
         y=" :${i}:"
         j="${i}"
         # But do not link any fakeroot injected path into our binaries!
         if [ -n "${addflag}" ]; then
            case "${i}" in *fakeroot*) continue;; esac
            k="${k} ${addflag}${i}"
         fi
      fi
   done
   eval "${varname}=\"${j}\""
   [ -n "${addflag}" ] && eval "${flagvarname}=\"${k}\""
   unset varname
}

ld_runtime_flags() {
   if [ -n "${ld_need_R_flags}" ]; then
      i=${IFS}
      IFS=:
      set -- ${LD_LIBRARY_PATH}
      IFS=${i}
      for i
      do
         # But do not link any fakeroot injected path into our binaries!
         case "${i}" in *fakeroot*) continue;; esac
         LDFLAGS="${LDFLAGS} ${ld_need_R_flags}${i}"
         _LDFLAGS="${_LDFLAGS} ${ld_need_R_flags}${i}"
      done
      export LDFLAGS
   fi
   # Disable it for a possible second run.
   ld_need_R_flags=
}

_cc_check_ever= _cc_check_testprog=
cc_check() {
   if [ -z "${_cc_check_ever}" ]; then
      _cc_check_ever=' '
      __occ=${cc_check_silent}
      __oCFLAGS=${_CFLAGS}

      cc_check_silent=y
      if cc_check -Werror; then
         _cc_check_ever=-Werror
         _CFLAGS=${__oCFLAGS}
         # Overcome a _GNU_SOURCE related glibc 2.32.3 bug (?!)
         if cc_check -Werror=implicit-function-declaration; then
            _cc_check_testprog=-Werror=implicit-function-declaration
         fi
      fi

      _CFLAGS=${__oCFLAGS}
      cc_check_silent=${__occ}
      unset __occ __oCFLAGS
   fi

   [ -n "${cc_check_silent}" ] || msg_nonl ' . CC %s .. ' "${1}"
   (
      trap "exit 11" ABRT BUS ILL SEGV # avoid error messages (really)
      ${CC} ${INCS} ${_cc_check_ever} \
            ${CFLAGS} ${_CFLAGS} ${1} ${EXTRA_CFLAGS} \
            ${LDFLAGS} ${_LDFLAGS} ${EXTRA_LDFLAGS} \
            -o ${tmp2} ${tmp}.c ${LIBS} || exit 1
      feat_no CROSS_BUILD || exit 0
      ${tmp2}
   ) >/dev/null
   if [ $? -eq 0 ]; then
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
   (
      trap "exit 11" ABRT BUS ILL SEGV # avoid error messages (really)
      ${CC} ${INCS} ${_cc_check_ever} \
            ${CFLAGS} ${_CFLAGS} ${EXTRA_CFLAGS} \
            ${LDFLAGS} ${_LDFLAGS} ${1}${2} ${EXTRA_LDFLAGS} \
            -o ${tmp2} ${tmp}.c ${LIBS} || exit 1
      feat_no CROSS_BUILD || exit 0
      ${tmp2}
   ) >/dev/null
   if [ $? -eq 0 ]; then
      [ -n "${3}" ] || _LDFLAGS="${_LDFLAGS} ${1}"
      [ -n "${cc_check_silent}" ] || msg 'yes'
      return 0
   fi
   [ -n "${cc_check_silent}" ] || msg 'no'
   return 1
}

dump_test_program=1
_check_preface() {
   variable=$1 topic=$2 define=$3

   echo '@@@'
   msg_nonl ' . %s ... ' "${topic}"
   #echo "/* checked ${topic} */" >> ${h}
   ${rm} -f ${tmp} ${tmp}.o ${tmp}.c
   if [ "${dump_test_program}" = 1 ]; then
      { echo '#include <'"${h_name}"'>'; cat; } | ${tee} ${tmp}.c
   else
      { echo '#include <'"${h_name}"'>'; cat; } > ${tmp}.c
   fi
}

without_check() {
   oneorzero=$1 variable=$2 topic=$3 define=$4 libs=$5 incs=$6

   echo '@@@'
   msg_nonl ' . %s ... ' "${topic}"

   if [ "${oneorzero}" = 1 ]; then
      if [ -n "${incs}" ] || [ -n "${libs}" ]; then
         echo "@ INCS<${incs}> LIBS<${libs}>"
         LIBS="${LIBS} ${libs}"
         INCS="${INCS} ${incs}"
      fi
      msg 'yes (deduced)'
      echo "${define}" >> ${h}
      eval have_${variable}=yes
      return 0
   else
      #echo "/* ${define} */" >> ${h}
      msg 'no (deduced)'
      eval unset have_${variable}
      return 1
   fi
}

compile_check() {
   variable=$1 topic=$2 define=$3

   _check_preface "${variable}" "${topic}" "${define}"

   __comp() (
      cd "${OBJDIR}" || exit 1
      echo "@CC ${@}"
      eval MAKEFLAGS= "${@}" && [ -f ${tmp_basename}.o ]
   )

   if __comp ${CC} -Dmx_SOURCE -I./ ${INCS} \
            ${CFLAGS} ${LDFLAGS} ${_cc_check_testprog} \
            -c ${tmp}.c \
            ${LIBS} ${libs} 2>&1; then
      msg 'yes'
      echo "${define}" >> ${h}
      eval have_${variable}=yes
      return 0
   else
      #echo "/* ${define} */" >> ${h}
      msg 'no'
      eval unset have_${variable}
      return 1
   fi
}

_link_mayrun() {
   run=$1 variable=$2 topic=$3 define=$4 libs=$5 incs=$6

   _check_preface "${variable}" "${topic}" "${define}"

   if feat_yes CROSS_BUILD; then
      if [ ${run} = 1 ]; then
         run=0
      fi
   fi

   __comp() (
      cd "${OBJDIR}" || exit 1
      echo "@CC ${@}"
      eval MAKEFLAGS= "${@}" && [ -f "${tmp}" ] &&
         { [ ${run} -eq 0 ] || "${tmp}"; }
   )

   if __comp ${CC} -Dmx_SOURCE -I./ ${INCS} ${incs} \
            ${CFLAGS} ${LDFLAGS} ${_cc_check_testprog} \
            -o ${tmp} ${tmp}.c \
            ${LIBS} ${libs}; then
      echo "@ INCS<${incs}> LIBS<${libs}>; executed: ${run}"
      msg 'yes'
      echo "${define}" >> ${h}
      LIBS="${LIBS} ${libs}"
      INCS="${INCS} ${incs}"
      eval have_${variable}=yes
      return 0
   else
      msg 'no'
      #echo "/* ${define} */" >> ${h}
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

xrun_check() {
   _link_mayrun 2 "${1}" "${2}" "${3}" "${4}" "${5}"
}

string_to_char_array() {
   ${awk} -v xy="${@}" 'BEGIN{
      # POSIX: unspecified behaviour.
      # Does not work for SunOS /usr/xpg4/bin/awk!
      if(split("abc", xya, "") == 3)
         i = split(xy, xya, "")
      else{
         j = length(xy)
         for(i = 0; j > 0; --j){
            xya[++i] = substr(xy, 1, 1)
            xy = substr(xy, 2)
         }
      }
      xya[++i] = "\\0"
      for(j = 1; j <= i; ++j){
         if(j != 1)
            printf ", "
         y = xya[j]
         if(y == "\012")
            y = "\\n"
         else if(y == "\\")
            y = "\\\\"
         printf "'"'"'%s'"'"'", y
      }
   }'
}

squeeze_ws() {
   echo "${*}" |
   ${sed} -e 's/^[ 	]\{1,\}//' -e 's/[ 	]\{1,\}$//' -e 's/[ 	]\{1,\}/ /g'
}

##  --  >8  - <<SUPPORT FUNS | RUNNING>> -  8<  --  ##

msg() {
   fmt=${1}
   shift
   printf -- "${fmt}\n" "${@}"
}

msg_nonl() {
   fmt=${1}
   shift
   printf -- "${fmt}" "${@}"
}

# Very easy checks for the operating system in order to be able to adjust paths
# or similar very basic things which we need to be able to go at all
os_early_setup

# Check those tools right now that we need before including $rc
msg 'Checking for basic utility set'
thecmd_testandset_fail awk awk
thecmd_testandset_fail rm rm
thecmd_testandset_fail tr tr
thecmd_testandset_fail pwd pwd

# Lowercase this now in order to isolate all the remains from case matters
OS_ORIG_CASE=${OS}
OS=`echo ${OS} | ${tr} '[A-Z]' '[a-z]'`
export OS

# But first of all, create new configuration and check whether it changed
[ -z "${OBJDIR}" ] && OBJDIR=.obj
OBJDIR=`${awk} -v OD="${OBJDIR}" 'BEGIN{
   if(index(OD, "/"))
      sub("/+$", "", OD)
   if(OD !~ /^\//){
      "'"${pwd}"'" | getline i
      close("'"${pwd}"'")
      OD = i "/" OD
   }
   print OD
   }'`

rc=./make.rc
env="${OBJDIR}"/mk-config.env
h="${OBJDIR}"/mk-config.h h_name=mk-config.h
mk="${OBJDIR}"/mk-config.mk

newmk="${OBJDIR}"/mk-nconfig.mk
oldmk="${OBJDIR}"/mk-oconfig.mk
newenv="${OBJDIR}"/mk-nconfig.env
newh="${OBJDIR}"/mk-nconfig.h
oldh="${OBJDIR}"/mk-oconfig.h
tmp0="${OBJDIR}"/___tmp
tmp=${tmp0}1_${$}
tmp_basename=___tmp1_${$}
tmp2=${tmp0}2_${$}

if [ -d "${OBJDIR}" ] || mkdir -p "${OBJDIR}"; then :; else
   msg 'ERROR: cannot create '"${OBJDIR}"' build directory'
   exit 1
fi

# !!
log="${OBJDIR}"/mk-config.log
exec 5>&2 > ${log} 2>&1

msg() {
   fmt=${1}
   shift
   printf -- "${fmt}\n" "${@}"
   printf -- "${fmt}\n" "${@}" >&5
}

msg_nonl() {
   fmt=${1}
   shift
   printf -- "${fmt}" "${@}"
   printf -- "${fmt}" "${@}" >&5
}

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
thecmd_testandset_fail getconf getconf
thecmd_testandset_fail grep grep

# Before we step ahead with the other utilities perform a path cleanup first.
path_check PATH

# awk(1) above
thecmd_testandset_fail basename basename
thecmd_testandset_fail cat cat
thecmd_testandset_fail chmod chmod
thecmd_testandset_fail cp cp
thecmd_testandset_fail cmp cmp
# grep(1) above
thecmd_testandset ln ln # only for tests
thecmd_testandset_fail mkdir mkdir
thecmd_testandset_fail mv mv
# rm(1) above
thecmd_testandset_fail sed sed
thecmd_testandset_fail sort sort
thecmd_testandset_fail tee tee
__PATH=${PATH}
thecmd_testandset chown chown ||
   PATH="/sbin:${PATH}" thecmd_set chown chown ||
   PATH="/usr/sbin:${PATH}" thecmd_set_fail chown chown
PATH=${__PATH}
thecmd_testandset_fail MAKE make
make=${MAKE}
export MAKE
thecmd_testandset strip strip

# For ./mx-test.sh only
thecmd_testandset_fail cksum cksum

# Update OPT_ options now, in order to get possible inter-dependencies right
option_update 0

# (No functions since some shells loose non-exported variables in traps)
trap "trap \"\" HUP INT TERM; exit 1" HUP INT TERM
trap "trap \"\" HUP INT TERM EXIT;\
   ${rm} -rf ${tmp0}.* ${tmp0}* \
      ${newmk} ${oldmk} ${newenv} ${newh} ${oldh}" EXIT

printf '#ifdef mx_SOURCE\n' > ${newh}

# Now that we have pwd(1) and options at least permit some more actions, set
# our build paths unless make-emerge.sh has been used; it would have created
# a makefile with the full paths otherwise
if [ -z "${CWDDIR}" ]; then
   CWDDIR=`${pwd}`
   CWDDIR=`oneslash "${CWDDIR}"`
fi
if [ -z "${TOPDIR}" ]; then
   TOPDIR=${CWDDIR}
fi
   INCDIR="${TOPDIR}"include/
   SRCDIR="${TOPDIR}"src/

MX_CWDDIR=${CWDDIR}
   MX_INCDIR=${INCDIR}
   MX_SRCDIR=${SRCDIR}
PS_DOTLOCK_CWDDIR=${CWDDIR}
   PS_DOTLOCK_INCDIR=${INCDIR}
   PS_DOTLOCK_SRCDIR=${SRCDIR}
NET_TEST_CWDDIR=${CWDDIR}
   NET_TEST_INCDIR=${INCDIR}
   NET_TEST_SRCDIR=${SRCDIR}
SU_CWDDIR=${CWDDIR}
   SU_INCDIR=${INCDIR}
   SU_SRCDIR=${SRCDIR}

# Our configuration options may at this point still contain shell snippets,
# we need to evaluate them in order to get them expanded, and we need those
# evaluated values not only in our new configuration file, but also at hand..
msg_nonl 'Evaluating all configuration items ... '
option_evaluate
msg 'done'

option_update 1

#
printf "#define VAL_UAGENT \"${VAL_SID}${VAL_MAILX}\"\n" >> ${newh}
printf "VAL_UAGENT = ${VAL_SID}${VAL_MAILX}\n" >> ${newmk}
printf "VAL_UAGENT=${VAL_SID}${VAL_MAILX};export VAL_UAGENT\n" >> ${newenv}

# The problem now is that the test should be able to run in the users linker
# and path environment, so we need to place the test: rule first, before
# injecting the relevant make variables.  Set up necessary environment
if [ -z "${VERBOSE}" ]; then
   printf -- "ECHO_CC = @echo '  'CC \$(@);\n" >> ${newmk}
   printf -- "ECHO_LINK = @echo '  'LINK \$(@);\n" >> ${newmk}
   printf -- "ECHO_GEN = @echo '  'GEN \$(@);\n" >> ${newmk}
   printf -- "ECHO_TEST = @\n" >> ${newmk}
   printf -- "ECHO_CMD = @echo '  CMD';\n" >> ${newmk}
fi
printf 'test: all\n\t$(ECHO_TEST)%s %smx-test.sh --check %s\n' \
   "${SHELL}" "${TOPDIR}" "./${VAL_SID}${VAL_MAILX}" >> ${newmk}
printf \
   'testnj: all\n\t$(ECHO_TEST)JOBNO=1 %s %smx-test.sh --check %s\n' \
   "${SHELL}" "${TOPDIR}" "./${VAL_SID}${VAL_MAILX}" >> ${newmk}

# Add the known utility and some other variables
printf "#define VAL_PS_DOTLOCK \"${VAL_SID}${VAL_MAILX}-dotlock\"\n" >> ${newh}
printf "VAL_PS_DOTLOCK = \$(VAL_UAGENT)-dotlock\n" >> ${newmk}
printf 'VAL_PS_DOTLOCK=%s;export VAL_PS_DOTLOCK\n' \
   "${VAL_SID}${VAL_MAILX}-dotlock" >> ${newenv}
if feat_yes DOTLOCK; then
   printf "#real below OPTIONAL_PS_DOTLOCK = \$(VAL_PS_DOTLOCK)\n" >> ${newmk}
fi

if feat_yes NET_TEST; then
   printf "#real below OPTIONAL_NET_TEST = net-test\n" >> ${newmk}
fi

for i in \
   CWDDIR TOPDIR OBJDIR INCDIR SRCDIR \
         MX_CWDDIR MX_INCDIR MX_SRCDIR \
         PS_DOTLOCK_CWDDIR PS_DOTLOCK_INCDIR PS_DOTLOCK_SRCDIR \
         NET_TEST_CWDDIR NET_TEST_INCDIR NET_TEST_SRCDIR \
         SU_CWDDIR SU_INCDIR SU_SRCDIR \
      awk basename cat chmod chown cp cmp grep getconf \
         ln mkdir mv pwd rm sed sort tee tr \
      MAKE MAKEFLAGS make SHELL strip \
      cksum; do
   eval j=\$${i}
   printf -- "${i} = ${j}\n" >> ${newmk}
   printf -- "${i}=%s;export ${i}\n" "`quote_string ${j}`" >> ${newenv}
done

# Build a basic set of INCS and LIBS according to user environment.
C_INCLUDE_PATH="${INCDIR}:${SRCDIR}:${C_INCLUDE_PATH}"
if path_is_absolute "${OBJDIR}"; then
   C_INCLUDE_PATH="${OBJDIR}:${C_INCLUDE_PATH}"
else
   C_INCLUDE_PATH="${CWDDIR}${OBJDIR}:${C_INCLUDE_PATH}"
fi
C_INCLUDE_PATH="${CWDDIR}include:${C_INCLUDE_PATH}"

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
cc_create_testfile
cc_hello
# This may also update ld_runtime_flags() (again)
cc_flags

for i in \
      COMMLINE \
      PATH C_INCLUDE_PATH LD_LIBRARY_PATH \
      CC CFLAGS LDFLAGS \
      INCS LIBS \
      OSFULLSPEC \
      ; do
   eval j="\$${i}"
   printf -- "${i}=%s;export ${i}\n" "`quote_string ${j}`" >> ${newenv}
done

# Now finally check whether we already have a configuration and if so, whether
# all those parameters are still the same.. or something has actually changed
config_updated=
if [ -f ${env} ] && ${cmp} ${newenv} ${env} >/dev/null 2>&1; then
   msg 'Configuration is up-to-date'
   exit 0
elif [ -f ${env} ]; then
   config_updated=1
   msg 'Configuration has been updated..'
else
   msg 'Shiny configuration..'
fi

### WE ARE STARTING OVER ###

# Time to redefine helper 1
config_exit() {
   ${rm} -f ${h} ${mk}
   exit ${1}
}

${mv} -f ${newenv} ${env}
[ -f ${h} ] && ${mv} -f ${h} ${oldh}
${mv} -f ${newh} ${h} # Note this has still #ifdef mx_SOURCE open
[ -f ${mk} ] && ${mv} -f ${mk} ${oldmk}
${mv} -f ${newmk} ${mk}

## Compile and link checking

tmp3=${tmp0}3$$

# (No function since some shells loose non-exported variables in traps)
trap "trap \"\" HUP INT TERM;\
   ${rm} -f ${oldh} ${h} ${oldmk} ${mk}; exit 1" \
      HUP INT TERM
trap "trap \"\" HUP INT TERM EXIT;\
   ${rm} -rf ${oldh} ${oldmk} ${tmp0}.* ${tmp0}*" EXIT

# Time to redefine helper 2
msg() {
   fmt=${1}
   shift
   printf "@ ${fmt}\n" "${@}"
   printf -- "${fmt}\n" "${@}" >&5
}
msg_nonl() {
   fmt=${1}
   shift
   printf "@ ${fmt}\n" "${@}"
   printf -- "${fmt}" "${@}" >&5
}

## Generics

echo '#define VAL_BUILD_OS "'"${OS_ORIG_CASE}"'"' >> ${h}

printf '#endif /* mx_SOURCE */\n\n' >> ${h} # Opened when it was $newh

[ -n "${OS_DEFINES}" ] && printf \
'#if defined mx_SOURCE || defined mx_EXT_SOURCE || defined su_SOURCE
'"${OS_DEFINES}"'
#endif /* mx_SOURCE || mx_EXT_SOURCE || su_SOURCE */

' >> ${h}

## SU

i=`${getconf} PAGESIZE 2>/dev/null`
[ $? -eq 0 ] || i=`${getconf} PAGE_SIZE 2>/dev/null`
if [ $? -ne 0 ]; then
   msg 'Cannot query PAGESIZE via getconf(1), assuming 4096'
   i=0x1000
fi
printf '#define su_PAGE_SIZE %su\n' "${i}" >> ${h}

# Generate SU <> OS error number mappings
msg_nonl ' . OS error mapping table generated ... '
feat_yes DEVEL && NV= || NV=noverbose
SRCDIR="${SRCDIR}" TARGET="${h}" awk="${awk}" rm="${rm}" sort="${sort}" \
      ${SHELL} "${TOPDIR}"mk/su-make-errors.sh ${NV} compile_time || {
   msg 'no'
   config_exit 1
}
msg 'yes'

## /SU

## Test for "basic" system-calls / functionality that is used by all parts
## of our program.  Once this is done fork away BASE_LIBS and other BASE_*
## macros to be used by only the subprograms (potentially).

if run_check clock_gettime 'clock_gettime(2)' \
   '#define su_HAVE_CLOCK_GETTIME' << \!
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
   '#define su_HAVE_CLOCK_GETTIME' '-lrt' << \!
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
   '#define su_HAVE_GETTIMEOFDAY' << \!
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

if run_check clock_nanosleep 'clock_nanosleep(2)' \
   '#define su_HAVE_CLOCK_NANOSLEEP' << \!
#include <time.h>
# include <errno.h>
int main(void){
   struct timespec ts;

   ts.tv_sec = 1;
   ts.tv_nsec = 100000;
   if(!clock_nanosleep(
#if defined _POSIX_MONOTONIC_CLOCK && _POSIX_MONOTONIC_CLOCK +0 > 0
         CLOCK_MONOTONIC
#else
         CLOCK_REALTIME
#endif
         , 0, &ts, (void*)0) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
elif run_check nanosleep 'nanosleep(2)' \
   '#define su_HAVE_NANOSLEEP' << \!
#include <time.h>
# include <errno.h>
int main(void){
   struct timespec ts;

   ts.tv_sec = 1;
   ts.tv_nsec = 100000;
   if(!nanosleep(&ts, (void*)0) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
elif run_check nanosleep 'nanosleep(2) (via -lrt)' \
   '#define su_HAVE_NANOSLEEP' '-lrt' << \!
#include <time.h>
# include <errno.h>
int main(void){
   struct timespec ts;

   ts.tv_sec = 1;
   ts.tv_nsec = 100000;
   if(!nanosleep(&ts, (void*)0) || errno != ENOSYS)
      return 0;
   return 1;
}
!
then
   :
# link_check is enough for this, that function is so old, trust the proto
elif link_check sleep 'sleep(3)' \
   '#define su_HAVE_SLEEP' << \!
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

if link_check userdb 'gete?[gu]id(2), getpwuid(3), getpwnam(3)' << \!
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
   msg 'That much Unix we indulge ourselves.'
   config_exit 1
fi

if run_check ftruncate 'ftruncate(2)' \
   '#define mx_HAVE_FTRUNCATE' << \!
#include <unistd.h>
#include <sys/types.h>
# include <errno.h>
int main(void){
   if(ftruncate(0, 0) != 0 && errno == ENOSYS)
      return 1;
   return 0;
}
!
then
   :
else
   # TODO support mx_HAVE_FTRUNCATE *everywhere*, do not require this syscall!
   msg 'ERROR: we require the ftruncate(2) system call.'
   config_exit 1
fi

if link_check sa_restart 'SA_RESTART (for sigaction(2))' << \!
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

if link_check setenv '(un)?setenv(3)' '#define mx_HAVE_SETENV' << \!
#include <stdlib.h>
int main(void){
   setenv("s-mailx", "i want to see it cute!", 1);
   unsetenv("s-mailx");
   return 0;
}
!
then
   :
elif link_check setenv 'putenv(3)' '#define mx_HAVE_PUTENV' << \!
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
   speed_t ospeed;

   tcgetattr(0, &tios);
   tcsetattr(0, TCSANOW | TCSADRAIN | TCSAFLUSH, &tios);
   ospeed = ((tcgetattr(0, &tios) == -1) ? B9600 : cfgetospeed(&tios));
   return 0;
}
!
then
   :
else
   msg 'ERROR: we require termios.h and the tc[gs]etattr() function family.'
   msg 'That much Unix we indulge ourselves.'
   config_exit 1
fi

if link_check fcntl_lock 'fcntl(2) file locking' << \!
#include <fcntl.h>
#include <unistd.h>
int main(void){
   struct flock flp;

   flp.l_type = F_RDLCK;
   flp.l_type = F_WRLCK;
   flp.l_start = 0;
   flp.l_whence = 0;
   flp.l_len = 0;

   fcntl(3, F_SETLK, &flp); /* (Really: no unlocking) */
   return 0;
}
!
then
   :
else
   msg 'ERROR: we require fcntl(2) based file locking.'
   config_exit 1
fi

## optional stuff

if link_check vsnprintf 'vsnprintf(3)' << \!
#include <stdarg.h>
#include <stdio.h>
static void dome(char *buf, size_t blen, ...){
   va_list ap;

   va_start(ap, blen);
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
   __va_copy() {
      link_check va_copy "va_copy(3) (as ${2})" \
         "#define mx_HAVE_N_VA_COPY
#define n_va_copy ${2}" <<_EOT
#include <stdarg.h>
#include <stdio.h>
#if ${1}
# if defined __va_copy && !defined va_copy
#  define va_copy __va_copy
# endif
#endif
static void dome2(char *buf, size_t blen, va_list src){
   va_list ap;

   va_copy(ap, src);
   vsnprintf(buf, blen, "%s", ap);
   va_end(ap);
}
static void dome(char *buf, size_t blen, ...){
   va_list ap;

   va_start(ap, blen);
   dome2(buf, blen, ap);
   va_end(ap);
}
int main(void){
   char b[20];

   dome(b, sizeof b, "string");
   return 0;
}
_EOT
   }
   __va_copy 0 va_copy || __va_copy 1 __va_copy
fi

run_check pathconf 'f?pathconf(2)' '#define su_HAVE_PATHCONF' << \!
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

run_check pipe2 'pipe2(2)' '#define mx_HAVE_PIPE2' << \!
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

link_check tcgetwinsize 'tcgetwinsize(3)' '#define mx_HAVE_TCGETWINSIZE' << \!
#include <termios.h>
int main(void){
   struct winsize ws;

   tcgetwinsize(0, &ws);
   return 0;
}
!

# We use this only then for now (need NOW+1)
run_check utimensat 'utimensat(2)' '#define su_HAVE_UTIMENSAT' << \!
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

link_check putc_unlocked 'putc_unlocked(3)' \
   '#define mx_HAVE_PUTC_UNLOCKED' <<\!
#include <stdio.h>
int main(void){
   putc_unlocked('@', stdout);
   return 0;
}
!

link_check fchdir 'fchdir(3)' '#define mx_HAVE_FCHDIR' << \!
#include <unistd.h>
int main(void){
   fchdir(0);
   return 0;
}
!

if link_check realpath 'realpath(3)' '#define mx_HAVE_REALPATH' << \!
#include <stdlib.h>
int main(void){
   char x_buf[4096], *x = realpath(".", x_buf);

   return (x != NULL) ? 0 : 1;
}
!
then
   if run_check realpath_malloc 'realpath(3) takes NULL' \
         '#define mx_HAVE_REALPATH_NULL' << \!
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

link_check tm_gmtoff 'struct tm::tm_gmtoff' '#define mx_HAVE_TM_GMTOFF' << \!
#include <time.h>
int main(void){
   time_t t;

   t = time((void*)0);
   return gmtime(&t)->tm_gmtoff != 0;
}
!

##
## optional and selectable
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

if feat_yes DOTLOCK; then
   if run_check prctl_dumpable 'prctl(2) + PR_SET_DUMPABLE' \
         '#define mx_HAVE_PRCTL_DUMPABLE' << \!
#include <sys/prctl.h>
# include <errno.h>
int main(void){
   if(!prctl(PR_SET_DUMPABLE, 0) || errno != ENOSYS)
      return 0;
   return 1;
}
!
   then
      :
   elif run_check procctl_trace_ctl 'procctl(2) + PROC_TRACE_CTL_DISABLE' \
         '#define mx_HAVE_PROCCTL_TRACE_CTL' << \!
#include <sys/procctl.h>
#include <unistd.h>
# include <errno.h>
int main(void){
   int disable_trace = PROC_TRACE_CTL_DISABLE;
   if(procctl(P_PID, getpid(), PROC_TRACE_CTL, &disable_trace) != -1 ||
         errno != ENOSYS)
      return 0;
   return 1;
}
!
   then
      :
   elif run_check prtrace_deny 'ptrace(2) + PT_DENY_ATTACH' \
         '#define mx_HAVE_PTRACE_DENY' << \!
#include <sys/ptrace.h>
# include <errno.h>
int main(void){
   if(ptrace(PT_DENY_ATTACH, 0, 0, 0) != -1 || errno != ENOSYS)
      return 0;
   return 1;
}
!
   then
      :
   elif run_check setpflags_protect 'setpflags(2) + __PROC_PROTECT' \
         '#define mx_HAVE_SETPFLAGS_PROTECT' << \!
#include <priv.h>
# include <errno.h>
int main(void){
   if(!setpflags(__PROC_PROTECT, 1) || errno != ENOSYS)
      return 0;
   return 1;
}
!
   then
      :
   fi
fi

### FORK AWAY SHARED BASE SERIES ###

BASE_CFLAGS=${CFLAGS}
BASE_INCS=`squeeze_ws "${INCS}"`
BASE_LDFLAGS=${LDFLAGS}
BASE_LIBS=`squeeze_ws "${LIBS}"`

## The remains are expected to be used only by the main MUA binary!
## (And possibly by test programs)

OPT_LOCALES=0
link_check setlocale 'setlocale(3)' '#define mx_HAVE_SETLOCALE' << \!
#include <locale.h>
int main(void){
   setlocale(LC_ALL, "");
   return 0;
}
!
[ -n "${have_setlocale}" ] && OPT_LOCALES=1

OPT_MULTIBYTE_CHARSETS=0
OPT_WIDE_GLYPHS=0
OPT_TERMINAL_CHARSET=0
if [ -n "${have_setlocale}" ]; then
   link_check c90amend1 'ISO/IEC 9899:1990/Amendment 1:1995' \
      '#define mx_HAVE_C90AMEND1' << \!
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
   [ -n "${have_c90amend1}" ] && OPT_MULTIBYTE_CHARSETS=1

   if [ -n "${have_c90amend1}" ]; then
      link_check wcwidth 'wcwidth(3)' '#define mx_HAVE_WCWIDTH' << \!
#include <wchar.h>
int main(void){
   wcwidth(L'c');
   return 0;
}
!
      [ -n "${have_wcwidth}" ] && OPT_WIDE_GLYPHS=1
   fi

   link_check nl_langinfo 'nl_langinfo(3)' '#define mx_HAVE_NL_LANGINFO' << \!
#include <langinfo.h>
#include <stdlib.h>
int main(void){
   nl_langinfo(DAY_1);
   return (nl_langinfo(CODESET) == NULL);
}
!
   [ -n "${have_nl_langinfo}" ] && OPT_TERMINAL_CHARSET=1
fi # have_setlocale

link_check fnmatch 'fnmatch(3)' '#define mx_HAVE_FNMATCH' << \!
#include <fnmatch.h>
int main(void){
   return (fnmatch("*", ".", FNM_PATHNAME | FNM_PERIOD) == FNM_NOMATCH);
}
!

link_check dirent_d_type 'struct dirent.d_type' \
   '#define mx_HAVE_DIRENT_TYPE' << \!
#include <dirent.h>
int main(void){
   struct dirent de;
   return !(de.d_type == DT_UNKNOWN ||
      de.d_type == DT_DIR || de.d_type == DT_LNK);
}
!

OPT_FLOCK=1
if link_check flock_lock 'flock(2) file locking (via fcntl.h)' \
      '#define mx_HAVE_FLOCK <fcntl.h>' << \!
#include <fcntl.h>
#include <unistd.h>
int main(void){
   flock(3, LOCK_SH | LOCK_NB);
   flock(3, LOCK_EX | LOCK_NB);
   flock(3, LOCK_UN);
   return 0;
}
!
then
   :
elif link_check flock_lock 'flock(2) file locking (via sys/file.h)' \
      '#define mx_HAVE_FLOCK <sys/file.h>' << \!
#include <sys/file.h>
#include <unistd.h>
int main(void){
   flock(3, LOCK_SH | LOCK_NB);
   flock(3, LOCK_EX | LOCK_NB);
   flock(3, LOCK_UN);
   return 0;
}
!
then
   :
else
   OPT_FLOCK=0
fi

##
## optional and selectable
##

# OPT_ICONV, VAL_ICONV {{{
if feat_yes ICONV; then
   if val_allof VAL_ICONV libc,iconv; then
      :
   else
      msg 'ERROR: VAL_ICONV with invalid entries: %s' "${VAL_ICONV}"
      config_exit 1
   fi

   # To be able to create tests we need to figure out which replacement
   # sequence the iconv(3) implementation creates
   ${cat} > ${tmp2}.c << \!
#include <stdio.h> /* For C89 NULL */
#include <string.h>
#include <iconv.h>
int main(void){
   char inb[16], oub[16], *inbp, *oubp;
   iconv_t id;
   size_t inl, oul;
   int rv;

   /* U+2013 */
   memcpy(inbp = inb, "\342\200\223", sizeof("\342\200\223"));
   inl = sizeof("\342\200\223") -1;
   oul = sizeof oub;
   oubp = oub;

   rv = 1;
   if((id = iconv_open("us-ascii", "utf-8")) == (iconv_t)-1)
      goto jleave;

   rv = 14;
   if(iconv(id, &inbp, &inl, &oubp, &oul) == (size_t)-1)
      goto jleave;

   *oubp = '\0';
   oul = (size_t)(oubp - oub);
   if(oul == 0)
      goto jleave;
   /* Character-wise replacement? */
   if(oul == 1){
      rv = 2;
      if(oub[0] == '?')
         goto jleave;
      rv = 3;
      if(oub[0] == '*')
         goto jleave;
      rv = 14;
      goto jleave;
   }

   /* Byte-wise replacement? */
   if(oul == sizeof("\342\200\223") -1){
      rv = 12;
      if(!memcmp(oub, "???????", sizeof("\342\200\223") -1))
         goto jleave;
      rv = 13;
      if(!memcmp(oub, "*******", sizeof("\342\200\223") -1))
         goto jleave;
      rv = 14;
   }

jleave:
   if(id != (iconv_t)-1)
      iconv_close(id);

   return rv;
}
!

   val_iconv_libc() {
      < ${tmp2}.c link_check iconv 'iconv(3)' \
            '#define mx_HAVE_ICONV'
      [ $? -eq 0 ] && return 0
      < ${tmp2}.c link_check iconv 'iconv(3), GNU libiconv redirect aware' \
            '#define mx_HAVE_ICONV' '' '-DLIBICONV_PLUG'
   }

   val_iconv_iconv() {
      < ${tmp2}.c link_check iconv 'iconv(3) functionality (via -liconv)' \
         '#define mx_HAVE_ICONV' '-liconv'
   }

   val_iconv_bye() {
      feat_bail_required ICONV
   }

   VAL_ICONV="${VAL_ICONV},bye"
   val_foreach_call VAL_ICONV val_iconv

   if feat_yes ICONV && feat_no CROSS_BUILD; then
      { ${tmp}; } >/dev/null 2>&1
      case ${?} in
      1)
         msg 'WARN: disabling ICONV due to faulty conversion/restrictions'
         feat_bail_required ICONV
         ;;
      2) echo 'MAILX_ICONV_MODE=2;export MAILX_ICONV_MODE;' >> ${env};;
      3) echo 'MAILX_ICONV_MODE=3;export MAILX_ICONV_MODE;' >> ${env};;
      12) echo 'MAILX_ICONV_MODE=12;export MAILX_ICONV_MODE;' >> ${env};;
      13) echo 'MAILX_ICONV_MODE=13;export MAILX_ICONV_MODE;' >> ${env};;
      *) msg 'WARN: will restrict iconv(3) tests due to unknown replacement';;
      esac
   fi
else
   feat_is_disabled ICONV
fi
# }}} OPT_ICONV, VAL_ICONV

if feat_yes NET; then
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
         '#define mx_HAVE_UNIX_SOCKETS' ||
      < ${tmp2}.c run_check af_unix 'AF_UNIX sockets (via -lnsl)' \
         '#define mx_HAVE_UNIX_SOCKETS' '-lnsl' ||
      < ${tmp2}.c run_check af_unix 'AF_UNIX sockets (via -lsocket -lnsl)' \
         '#define mx_HAVE_UNIX_SOCKETS' '-lsocket -lnsl'
fi

if feat_yes NET; then
   ${cat} > ${tmp2}.c << \!
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
         '#define mx_HAVE_NET' ||
      < ${tmp2}.c run_check sockets 'sockets (via -lnsl)' \
         '#define mx_HAVE_NET' '-lnsl' ||
      < ${tmp2}.c run_check sockets 'sockets (via -lsocket -lnsl)' \
         '#define mx_HAVE_NET' '-lsocket -lnsl' ||
      feat_bail_required NET
else
   feat_is_disabled NET
fi # feat_yes NET

feat_yes NET &&
   link_check sockopt '[gs]etsockopt(2)' '#define mx_HAVE_SOCKOPT' << \!
#include <sys/socket.h>
#include <stdlib.h>
# include <errno.h>
int main(void){
   socklen_t sol;
   int sockfd = 3, soe;

   sol = sizeof soe;
   if(getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &soe, &sol) == -1 &&
         errno == ENOSYS)
      return 1;
   if(setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, NULL, 0) == -1 &&
         errno == ENOSYS)
      return 1;
   return 0;
}
!

feat_yes NET &&
   link_check nonblocksock 'non-blocking sockets' \
      '#define mx_HAVE_NONBLOCKSOCK' << \!
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
# include <errno.h>
int main(void){
   fd_set fdset;
   struct timeval tv;
   struct sockaddr_in sin;
   socklen_t sol;
   int sofd, soe;

   if((sofd = socket(AF_INET, SOCK_STREAM, 0)) == -1 && errno == ENOSYS)
      return 1;
   if(fcntl(sofd, F_SETFL, O_NONBLOCK) != 0)
      return 1;

   sin.sin_family = AF_INET;
   sin.sin_addr.s_addr = inet_addr("127.0.0.1");
   sin.sin_port = htons(80);
   if(connect(sofd, &sin, sizeof sin) == -1 && errno == ENOSYS)
      return 1;

   FD_ZERO(&fdset);
   FD_SET(sofd, &fdset);
   tv.tv_sec = 10;
   tv.tv_usec = 0;
   if((soe = select(sofd + 1, NULL, &fdset, NULL, &tv)) == 1){
      sol = sizeof soe;
      getsockopt(sofd, SOL_SOCKET, SO_ERROR, &soe, &sol);
      if(soe == 0)
         return 0;
   }else if(soe == -1 && errno == ENOSYS)
      return 1;

   close(sofd);
   return 0;
}
!

if feat_yes NET; then
   link_check getaddrinfo 'getaddrinfo(3)' \
      '#define mx_HAVE_GETADDRINFO' << \!
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

if feat_yes NET && [ -z "${have_getaddrinfo}" ]; then
   compile_check arpa_inet_h '<arpa/inet.h>' \
      '#define mx_HAVE_ARPA_INET_H' << \!
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
!

   ${cat} > ${tmp2}.c << \!
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#ifdef mx_HAVE_ARPA_INET_H
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
      feat_bail_required NET
fi

feat_yes NET && [ -n "${have_sockopt}" ] &&
   link_check so_xtimeo 'SO_{RCV,SND}TIMEO' '#define mx_HAVE_SO_XTIMEO' << \!
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

feat_yes NET && [ -n "${have_sockopt}" ] &&
   link_check so_linger 'SO_LINGER' '#define mx_HAVE_SO_LINGER' << \!
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

VAL_TLS_FEATURES=
if feat_yes TLS; then # {{{
   # {{{ LibreSSL decided to define OPENSSL_VERSION_NUMBER with a useless value
   # instead of keeping it at the one that corresponds to the OpenSSL at fork
   # time: we need to test it first in order to get things right
   if compile_check _xtls 'TLS (LibreSSL)' \
      '#define mx_HAVE_TLS mx_TLS_IMPL_RESSL
      #define mx_HAVE_XTLS 0 /* 0 for LibreSSL */' << \!
#include <openssl/opensslv.h>
#ifdef LIBRESSL_VERSION_NUMBER
#else
# error nope
#endif
int nonempty;
!
   then
      ossl_v1_1=
      VAL_TLS_FEATURES=libressl,-tls-rand-file
   # TODO BORINGSSL, generalize this mess!
   elif compile_check _xtls 'TLS (OpenSSL >= v3.0.0)' \
      '#define mx_HAVE_TLS mx_TLS_IMPL_OPENSSL
      #define mx_HAVE_XTLS 0x30000' << \!
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER + 0 >= 0x30000000L
#else
# error nope
#endif
int nonempty;
!
   then
      ossl_v1_1=1
      VAL_TLS_FEATURES=libssl-0x30000,-tls-rand-file
   elif compile_check _xtls 'TLS (OpenSSL >= v1.1.1)' \
      '#define mx_HAVE_TLS mx_TLS_IMPL_OPENSSL
      #define mx_HAVE_XTLS 0x10101' << \!
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER + 0 >= 0x1010100fL
#else
# error nope
#endif
int nonempty;
!
   then
      ossl_v1_1=1
      VAL_TLS_FEATURES=libssl-0x10100,-tls-rand-file
   elif compile_check _xtls 'TLS (OpenSSL >= v1.1.0)' \
      '#define mx_HAVE_TLS mx_TLS_IMPL_OPENSSL
      #define mx_HAVE_XTLS 0x10100
      #define mx_XTLS_HAVE_RAND_FILE' << \!
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER + 0 >= 0x10100000L
#else
# error nope
#endif
int nonempty;
!
   then
      ossl_v1_1=1
      VAL_TLS_FEATURES=libssl-0x10100,+tls-rand-file
   elif compile_check _xtls 'TLS (OpenSSL)' \
      '#define mx_HAVE_TLS mx_TLS_IMPL_OPENSSL
      #define mx_HAVE_XTLS 0x10000
      #define mx_XTLS_HAVE_RAND_FILE' << \!
#include <openssl/opensslv.h>
#ifdef OPENSSL_VERSION_NUMBER
#else
# error nope
#endif
int nonempty;
!
   then
      ossl_v1_1=
      VAL_TLS_FEATURES=libssl-0x10000,+tls-rand-file
   else
      feat_bail_required TLS
   fi # }}}

   if feat_yes TLS; then # {{{
      if [ -n "${ossl_v1_1}" ]; then
         without_check 1 xtls 'TLS new style TLS_client_method(3ssl)' \
            '#define mx_XTLS_CLIENT_METHOD TLS_client_method
            #define mx_XTLS_SERVER_METHOD TLS_server_method' \
            '-lssl -lcrypto'
      elif link_check xtls 'TLS new style TLS_client_method(3ssl)' \
            '#define mx_XTLS_CLIENT_METHOD TLS_client_method
            #define mx_XTLS_SERVER_METHOD TLS_server_method' \
            '-lssl -lcrypto' << \!
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
      elif link_check xtls 'TLS old style SSLv23_client_method(3ssl)' \
            '#define mx_XTLS_CLIENT_METHOD SSLv23_client_method
            #define mx_XTLS_SERVER_METHOD SSLv23_server_method' \
            '-lssl -lcrypto' << \!
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
         feat_bail_required TLS
      fi
   fi # }}}

   if feat_yes TLS; then # {{{
      if [ -n "${ossl_v1_1}" ]; then
         without_check 1 xtls_stack_of '*SSL STACK_OF()' \
            '#define mx_XTLS_HAVE_STACK_OF'
      elif compile_check xtls_stack_of '*SSL STACK_OF()' \
            '#define mx_XTLS_HAVE_STACK_OF' << \!
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
      then
         :
      fi

      if [ -n "${ossl_v1_1}" ]; then
         without_check 1 xtls_conf 'TLS OpenSSL_modules_load_file(3ssl)' \
            '#define mx_XTLS_HAVE_CONFIG'
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+modules-load-file"
      elif link_check xtls_conf \
            'TLS OpenSSL_modules_load_file(3ssl) support' \
            '#define mx_XTLS_HAVE_CONFIG' << \!
#include <stdio.h> /* For C89 NULL */
#include <openssl/conf.h>
int main(void){
   CONF_modules_load_file(NULL, NULL, CONF_MFLAGS_IGNORE_MISSING_FILE);
#if mx_HAVE_XTLS < 0x10100
   CONF_modules_free();
#endif
   return 0;
}
!
      then
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+modules-load-file"
      else
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},-modules-load-file"
      fi

      if [ -n "${ossl_v1_1}" ]; then
         without_check 1 xtls_conf_ctx 'TLS SSL_CONF_CTX support' \
            '#define mx_XTLS_HAVE_CONF_CTX'
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+conf-ctx"
      elif link_check xtls_conf_ctx 'TLS SSL_CONF_CTX support' \
         '#define mx_XTLS_HAVE_CONF_CTX' << \!
#include <openssl/ssl.h>
#include <openssl/err.h>
int main(void){
   SSL_CTX *ctx = SSL_CTX_new(mx_XTLS_CLIENT_METHOD());
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
      then
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+conf-ctx"
      else
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},-conf-ctx"
      fi

      if [ -n "${ossl_v1_1}" ]; then
         without_check 1 xtls_ctx_config 'TLS SSL_CTX_config(3ssl)' \
            '#define mx_XTLS_HAVE_CTX_CONFIG'
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+ctx-config"
      elif [ -n "${have_xtls_conf}" ] && [ -n "${have_xtls_conf_ctx}" ] &&
            link_check xtls_ctx_config 'TLS SSL_CTX_config(3ssl)' \
               '#define mx_XTLS_HAVE_CTX_CONFIG' << \!
#include <stdio.h> /* For C89 NULL */
#include <openssl/ssl.h>
int main(void){
   SSL_CTX_config(NULL, "SOMEVAL");
   return 0;
}
!
      then
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+ctx-config"
      else
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},-ctx-config"
      fi

      if [ -n "${ossl_v1_1}" ] && [ -n "${have_xtls_conf_ctx}" ]; then
         without_check 1 xtls_set_maxmin_proto \
            'TLS SSL_CTX_set_min_proto_version(3ssl)' \
            '#define mx_XTLS_HAVE_SET_MIN_PROTO_VERSION'
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+ctx-set-maxmin-proto"
      elif link_check xtls_set_maxmin_proto \
         'TLS SSL_CTX_set_min_proto_version(3ssl)' \
         '#define mx_XTLS_HAVE_SET_MIN_PROTO_VERSION' << \!
#include <stdio.h> /* For C89 NULL */
#include <openssl/ssl.h>
int main(void){
   SSL_CTX_set_min_proto_version(NULL, 0);
   SSL_CTX_set_max_proto_version(NULL, 10);
   return 0;
}
!
      then
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+ctx-set-maxmin-proto"
      else
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},-ctx-set-maxmin-proto"
      fi

      if [ -n "${ossl_v1_1}" ] && [ -n "${have_xtls_conf_ctx}" ]; then
         without_check 1 xtls_set_ciphersuites \
            'TLSv1.3 SSL_CTX_set_ciphersuites(3ssl)' \
            '#define mx_XTLS_HAVE_SET_CIPHERSUITES'
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+ctx-set-ciphersuites"
      elif link_check xtls_set_ciphersuites \
         'TLSv1.3 SSL_CTX_set_ciphersuites(3ssl)' \
         '#define mx_XTLS_HAVE_SET_CIPHERSUITES' << \!
#include <stdio.h> /* For C89 NULL */
#include <openssl/ssl.h>
int main(void){
   SSL_CTX_set_ciphersuites(NULL, NULL);
   return 0;
}
!
      then
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},+ctx-set-ciphersuites"
      else
         VAL_TLS_FEATURES="${VAL_TLS_FEATURES},-ctx-set-ciphersuites"
      fi
   fi # feat_yes TLS }}}

   if feat_yes TLS; then # digest etc algorithms {{{
      if feat_yes TLS_ALL_ALGORITHMS; then
         if [ -n "${ossl_v1_1}" ]; then
            without_check 1 tls_all_algo 'TLS_ALL_ALGORITHMS support' \
               '#define mx_HAVE_TLS_ALL_ALGORITHMS'
         elif link_check tls_all_algo 'TLS all-algorithms support' \
            '#define mx_HAVE_TLS_ALL_ALGORITHMS' << \!
#include <openssl/evp.h>
int main(void){
   OpenSSL_add_all_algorithms();
   EVP_get_cipherbyname("two cents i never exist");
#if mx_HAVE_XTLS < 0x10100
   EVP_cleanup();
#endif
   return 0;
}
!
         then
            :
         else
            feat_bail_required TLS_ALL_ALGORITHMS
         fi
      elif [ -n "${ossl_v1_1}" ]; then
         without_check 1 tls_all_algo \
            'TLS all-algorithms (always available in v1.1.0+)' \
            '#define mx_HAVE_TLS_ALL_ALGORITHMS'
      fi

      # Blake
      link_check tls_blake2 'TLS: BLAKE2 digests' \
            '#define mx_XTLS_HAVE_BLAKE2' << \!
#include <openssl/evp.h>
int main(void){
   EVP_blake2b512();
   EVP_blake2s256();
   return 0;
}
!

      # SHA-3
      link_check tls_sha3 'TLS: SHA-3 digests' \
            '#define mx_XTLS_HAVE_SHA3' << \!
#include <openssl/evp.h>
int main(void){
   EVP_sha3_512();
   EVP_sha3_384();
   EVP_sha3_256();
   EVP_sha3_224();
   return 0;
}
!

      if feat_yes MD5 && feat_no NOEXTMD5; then
         run_check tls_md5 'TLS: MD5 digest' '#define mx_XTLS_HAVE_MD5' << \!
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

#define su_cs_is_xdigit(n) ((n) > 9 ? (n) - 10 + 'a' : (n) + '0')
   for(i = 0; i < sizeof(hex) / 2; i++){
      j = i << 1;
      hex[j] = su_cs_is_xdigit((dig[i] & 0xf0) >> 4);
      hex[++j] = su_cs_is_xdigit(dig[i] & 0x0f);
   }
   return !!memcmp("6d7d0a3d949da2e96f2aa010f65d8326", hex, sizeof(hex));
}
!
      fi
   else
      feat_bail_required TLS_ALL_ALGORITHMS # feat_is_disabled?
   fi # }}}
else
   feat_is_disabled TLS
   feat_is_disabled TLS_ALL_ALGORITHMS
fi # }}} feat_yes TLS
printf '#ifdef mx_SOURCE\n' >> ${h}
printf '#define VAL_TLS_FEATURES ",'"${VAL_TLS_FEATURES}"',"\n' >> ${h}
printf '#endif /* mx_SOURCE */\n' >> ${h}

if [ "${have_xtls}" = yes ]; then
   OPT_SMIME=1
else
   OPT_SMIME=0
fi

# VAL_RANDOM {{{
if val_allof VAL_RANDOM \
      "arc4,tls,libgetrandom,sysgetrandom,getentropy,urandom,builtin,error"; \
      then
   :
else
   msg 'ERROR: VAL_RANDOM with invalid entries: %s' "${VAL_RANDOM}"
   config_exit 1
fi

# Random implementations which completely replace our builtin machine

val_random_arc4() {
   link_check arc4random 'VAL_RANDOM: arc4random(3)' \
      '#define mx_HAVE_RANDOM mx_RANDOM_IMPL_ARC4' << \!
#include <stdlib.h>
int main(void){
   arc4random();
   return 0;
}
!
}

val_random_tls() {
   if feat_yes TLS; then
      msg ' . VAL_RANDOM: tls ... yes'
      echo '#define mx_HAVE_RANDOM mx_RANDOM_IMPL_TLS' >> ${h}
      # Avoid reseeding, all we need is a streamy random producer
      link_check xtls_rand_drbg_set_reseed_defaults \
         'RAND_DRBG_set_reseed_defaults(3ssl)' \
         '#define mx_XTLS_HAVE_SET_RESEED_DEFAULTS' << \!
#include <openssl/rand_drbg.h>
int main(void){
   return (RAND_DRBG_set_reseed_defaults(0, 0, 0, 0) != 0);
}
!
      return 0
   else
      msg ' . VAL_RANDOM: tls ... no'
      return 1
   fi
}

# The remaining random implementation are only used to seed our builtin
# machine; we are prepared to handle failures of those, meaning that we have
# a homebrew seeder; that tries to yield the time slice once, via
# sched_yield(2) if available, nanosleep({0,0},) otherwise
val__random_yield_ok=
val__random_check_yield() {
   [ -n "${val__random_yield_ok}" ] && return
   val__random_yield_ok=1
   link_check sched_yield 'sched_yield(2)' '#define mx_HAVE_SCHED_YIELD' << \!
#include <sched.h>
int main(void){
   sched_yield();
   return 0;
}
!
}

val_random_libgetrandom() {
   val__random_check_yield
   link_check getrandom 'VAL_RANDOM: getrandom(3) (in sys/random.h)' \
      '#define mx_HAVE_RANDOM mx_RANDOM_IMPL_GETRANDOM
      #define mx_RANDOM_GETRANDOM_FUN(B,S) getrandom(B, S, 0)
      #define mx_RANDOM_GETRANDOM_H <sys/random.h>' <<\!
#include <sys/random.h>
int main(void){
   char buf[256];
   getrandom(buf, sizeof buf, 0);
   return 0;
}
!
}

val_random_sysgetrandom() {
   val__random_check_yield
   link_check getrandom 'VAL_RANDOM: getrandom(2) (via syscall(2))' \
      '#define mx_HAVE_RANDOM mx_RANDOM_IMPL_GETRANDOM
      #define mx_RANDOM_GETRANDOM_FUN(B,S) syscall(SYS_getrandom, B, S, 0)
      #define mx_RANDOM_GETRANDOM_H <sys/syscall.h>' <<\!
#include <sys/syscall.h>
int main(void){
   char buf[256];
   syscall(SYS_getrandom, buf, sizeof buf, 0);
   return 0;
}
!
}

val_random_getentropy() {
   val__random_check_yield
   link_check getentropy 'VAL_RANDOM: getentropy(3)' \
      '#define mx_HAVE_RANDOM mx_RANDOM_IMPL_GETENTROPY' <<\!
# include <errno.h>
#include <limits.h>
#include <unistd.h>
# ifndef GETENTROPY_MAX
#  define GETENTROPY_MAX 256
# endif
int main(void){
   typedef char cta[GETENTROPY_MAX >= 256 ? 1 : -1];
   char buf[GETENTROPY_MAX];

   if(!getentropy(buf, sizeof buf) || errno != ENOSYS)
      return 0;
   return 1;
}
!
}

val_random_urandom() {
   val__random_check_yield
   msg_nonl ' . VAL_RANDOM: /dev/urandom ... '
   if feat_yes CROSS_BUILD; then
      msg 'yes (unchecked)'
      echo '#define mx_HAVE_RANDOM mx_RANDOM_IMPL_URANDOM' >> ${h}
   elif [ -f /dev/urandom ]; then
      msg yes
      echo '#define mx_HAVE_RANDOM mx_RANDOM_IMPL_URANDOM' >> ${h}
   else
      msg no
      return 1
   fi
   return 0
}

val_random_builtin() {
   val__random_check_yield
   msg_nonl ' . VAL_RANDOM: builtin ... '
   if [ -n "${have_no_subsecond_time}" ]; then
      msg 'no\nERROR: %s %s' 'without a specialized PRG ' \
         'one of clock_gettime(2) and gettimeofday(2) is required.'
      config_exit 1
   else
      msg yes
      echo '#define mx_HAVE_RANDOM mx_RANDOM_IMPL_BUILTIN' >> ${h}
   fi
}

val_random_error() {
   msg 'ERROR: VAL_RANDOM search reached "error" entry'
   config_exit 42
}

VAL_RANDOM="${VAL_RANDOM},error"
val_foreach_call VAL_RANDOM val_random
# }}} VAL_RANDOM

if feat_yes GSSAPI; then # {{{
   ${cat} > ${tmp2}.c << \!
#include <gssapi/gssapi.h>
int main(void){
   gss_import_name(0, 0, GSS_C_NT_HOSTBASED_SERVICE, 0);
   gss_init_sec_context(0,0,0,0,0,0,0,0,0,0,0,0,0);
   return 0;
}
!
   ${sed} -e '1s/gssapi\///' < ${tmp2}.c > ${tmp3}.c

   if acmd_set i krb5-config; then
      GSS_LIBS="`CFLAGS= ${i} --libs gssapi`"
      GSS_INCS="`CFLAGS= ${i} --cflags`"
      i='GSS-API via krb5-config(1)'
   else
      GSS_LIBS='-lgssapi'
      GSS_INCS=
      i='GSS-API in gssapi/gssapi.h, libgssapi'
   fi
   if < ${tmp2}.c link_check gss \
         "${i}" '#define mx_HAVE_GSSAPI' "${GSS_LIBS}" "${GSS_INCS}" ||\
      < ${tmp3}.c link_check gss \
         'GSS-API in gssapi.h, libgssapi' \
         '#define mx_HAVE_GSSAPI
         #define GSSAPI_REG_INCLUDE' \
         '-lgssapi' ||\
      < ${tmp2}.c link_check gss 'GSS-API in libgssapi_krb5' \
         '#define mx_HAVE_GSSAPI' \
         '-lgssapi_krb5' ||\
      < ${tmp3}.c link_check gss \
         'GSS-API in libgssapi, OpenBSD-style (pre 5.3)' \
         '#define mx_HAVE_GSSAPI
         #define GSS_REG_INCLUDE' \
         '-lgssapi -lkrb5 -lcrypto' \
         '-I/usr/include/kerberosV' ||\
      < ${tmp2}.c link_check gss 'GSS-API in libgss' \
         '#define mx_HAVE_GSSAPI' \
         '-lgss' ||\
      link_check gss 'GSS-API in libgssapi_krb5, old-style' \
         '#define mx_HAVE_GSSAPI
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
   feat_is_disabled GSSAPI
fi # feat_yes GSSAPI }}}

if feat_yes IDNA; then # {{{
   if val_allof VAL_IDNA "idnkit,idn2,idn"; then
      :
   else
      msg 'ERROR: VAL_IDNA with invalid entries: %s' "${VAL_IDNA}"
      config_exit 1
   fi

   val_idna_idn2() {
      link_check idna 'OPT_IDNA->VAL_IDNA: GNU Libidn2' \
         '#define mx_HAVE_IDNA n_IDNA_IMPL_LIBIDN2' '-lidn2' << \!
#include <idn2.h>
int main(void){
   char *idna_utf8, *idna_lc;

   if(idn2_to_ascii_8z("does.this.work", &idna_utf8,
         IDN2_NONTRANSITIONAL | IDN2_TRANSITIONAL) != IDN2_OK)
      return 1;
   if(idn2_to_unicode_8zlz(idna_utf8, &idna_lc, 0) != IDN2_OK)
      return 1;
   idn2_free(idna_lc);
   idn2_free(idna_utf8);
   return 0;
}
!
   }

   val_idna_idn() {
      link_check idna 'OPT_IDNA->VAL_IDNA: GNU Libidn' \
         '#define mx_HAVE_IDNA n_IDNA_IMPL_LIBIDN' '-lidn' << \!
#include <idna.h>
#include <idn-free.h>
#include <stringprep.h> /* XXX we actually use our own iconv instead */
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
   }

   val_idna_idnkit() {
      link_check idna 'OPT_IDNA->VAL_IDNA: idnkit' \
         '#define mx_HAVE_IDNA n_IDNA_IMPL_IDNKIT' '-lidnkit' << \!
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
   r = idn_decodename(IDN_DECODE_APP, ace_name, local_name,sizeof(local_name));
   if (r != idn_success) {
      fprintf(stderr, "idn_decodename failed: %s\n", idn_result_tostring(r));
      return 1;
   }
   return 0;
}
!
   }

   val_idna_bye() {
      feat_bail_required IDNA
   }

   VAL_IDNA="${VAL_IDNA},bye"
   val_foreach_call VAL_IDNA val_idna
else
   feat_is_disabled IDNA
fi # }}} IDNA

if feat_yes REGEX; then
   if link_check regex 'regular expressions' '#define mx_HAVE_REGEX' << \!
#include <regex.h>
#include <stdlib.h>
int main(void){
   size_t xret;
   int status;
   regex_t re;

   status = regcomp(&re, ".*bsd", REG_EXTENDED | REG_ICASE | REG_NOSUB);
   xret = regerror(status, &re, NULL, 0);
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
   feat_is_disabled REGEX
fi

if feat_yes MLE; then
   if [ -n "${have_c90amend1}" ]; then
      have_mle=1
      echo '#define mx_HAVE_MLE' >> ${h}
   else
      feat_bail_required MLE
   fi
else
   feat_is_disabled MLE
fi

if feat_yes HISTORY; then
   if [ -n "${have_mle}" ]; then
      echo '#define mx_HAVE_HISTORY' >> ${h}
   else
      feat_is_unsupported HISTORY
   fi
else
   feat_is_disabled HISTORY
fi

if feat_yes KEY_BINDINGS; then
   if [ -n "${have_mle}" ]; then
      echo '#define mx_HAVE_KEY_BINDINGS' >> ${h}
   else
      feat_is_unsupported KEY_BINDINGS
   fi
else
   feat_is_disabled KEY_BINDINGS
fi

if feat_yes TERMCAP; then # {{{
   ADDINC=
   __termcaplib() {
      link_check termcap "termcap(5) (via ${4}${ADDINC})" \
         "#define mx_HAVE_TERMCAP${3}" "${1}" "${ADDINC}" << _EOT
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
      link_check terminfo "terminfo(5) (via ${2}${ADDINC})" \
         '#define mx_HAVE_TERMCAP
         #define mx_HAVE_TERMCAP_CURSES
         #define mx_HAVE_TERMINFO' "${1}" "${ADDINC}" << _EOT
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
      ADDINC=
      do_me() {
         xbail=
         __terminfolib -ltinfo -ltinfo ||
            __terminfolib -lcurses -lcurses ||
            __terminfolib -lcursesw -lcursesw ||
         xbail=y
      }
      do_me
      if [ -n "${xbail}" ] && [ -d /usr/local/include/ncurses ]; then
         ADDINC=' -I/usr/local/include/ncurses'
         do_me
      fi
      if [ -n "${xbail}" ] && [ -d /usr/include/ncurses ]; then
         ADDINC=' -I/usr/include/ncurses'
         do_me
      fi
      [ -n "${xbail}" ] && feat_bail_required TERMCAP_VIA_TERMINFO
   fi

   if [ -z "${have_terminfo}" ]; then
      ADDINC=
      do_me() {
         xbail=
         __termcaplib -ltermcap '' '' '-ltermcap' ||
            __termcaplib -ltermcap '#include <curses.h>' '
               #define mx_HAVE_TERMCAP_CURSES' \
               'curses.h / -ltermcap' ||
            __termcaplib -lcurses '#include <curses.h>' '
               #define mx_HAVE_TERMCAP_CURSES' \
               'curses.h / -lcurses' ||
            __termcaplib -lcursesw '#include <curses.h>' '
               #define mx_HAVE_TERMCAP_CURSES' \
               'curses.h / -lcursesw' ||
            xbail=y
      }
      do_me
      if [ -n "${xbail}" ] && [ -d /usr/local/include/ncurses ]; then
         ADDINC=' -I/usr/local/include/ncurses'
         do_me
      fi
      if [ -n "${xbail}" ] && [ -d /usr/include/ncurses ]; then
         ADDINC=' -I/usr/include/ncurses'
         do_me
      fi
      [ -n "${xbail}" ] && feat_bail_required TERMCAP

      if [ -n "${have_termcap}" ]; then
         run_check tgetent_null \
            "tgetent(3) of termcap(5) takes NULL buffer" \
            "#define mx_HAVE_TGETENT_NULL_BUF" << _EOT
#include <stdio.h> /* For C89 NULL */
#include <stdlib.h>
#ifdef mx_HAVE_TERMCAP_CURSES
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
   unset ADDINC
else # }}}
   feat_is_disabled TERMCAP
   feat_is_disabled TERMCAP_VIA_TERMINFO
fi

##
## Final feat_def's XXX should be loop over OPTIONs
##

feat_def ALWAYS_UNICODE_LOCALE
feat_def AMALGAMATION 0
feat_def CMD_CSOP
feat_def CMD_FOP
feat_def CMD_VEXPR
feat_def COLOUR
feat_def CROSS_BUILD
feat_def DOTLOCK
feat_def FILTER_HTML_TAGSOUP
if feat_yes FILTER_QUOTE_FOLD; then
   if [ -n "${have_c90amend1}" ] && [ -n "${have_wcwidth}" ]; then
      echo '#define mx_HAVE_FILTER_QUOTE_FOLD' >> ${h}
   else
      feat_bail_required FILTER_QUOTE_FOLD
   fi
else
   feat_is_disabled FILTER_QUOTE_FOLD
fi
feat_def DOCSTRINGS
feat_def ERRORS
feat_def IMAP
feat_def IMAP_SEARCH
feat_def MAILCAP
feat_def MAILDIR
feat_def MD5 # XXX only sockets
feat_def MTA_ALIASES
feat_def NETRC
feat_def POP3
feat_def SMIME
feat_def SMTP
feat_def SPAM_FILTER
if feat_def SPAM_SPAMC; then
   if acmd_set i spamc; then
      echo "#define SPAM_SPAMC_PATH \"${i}\"" >> ${h}
   fi
fi
if feat_yes SPAM_SPAMC || feat_yes SPAM_FILTER; then
   echo '#define mx_HAVE_SPAM' >> ${h}
else
   echo '/* mx_HAVE_SPAM */' >> ${h}
fi
feat_def UISTRINGS
feat_def USE_PKGSYS

feat_def ASAN_ADDRESS 0
feat_def ASAN_MEMORY 0
feat_def USAN 0
feat_def DEBUG 0
feat_def DEVEL 0
feat_def NOMEMDBG 0

##
## Summarizing
##

option_update 2

INCS=`squeeze_ws "${INCS}"`
LIBS=`squeeze_ws "${LIBS}"`

MX_CFLAGS=${CFLAGS}
   MX_INCS=${INCS}
   MX_LDFLAGS=${LDFLAGS}
   MX_LIBS=${LIBS}
SU_CFLAGS=${BASE_CFLAGS}
   SU_CXXFLAGS=
   SU_INCS=${BASE_INCS}
   SU_LDFLAGS=${BASE_LDFLAGS}
   SU_LIBS=${BASE_LIBS}
PS_DOTLOCK_CFLAGS=${BASE_CFLAGS}
   PS_DOTLOCK_INCS=${BASE_INCS}
   PS_DOTLOCK_LDFLAGS=${BASE_LDFLAGS}
   PS_DOTLOCK_LIBS=${BASE_LIBS}
NET_TEST_CFLAGS=${CFLAGS}
   NET_TEST_INCS=${INCS}
   NET_TEST_LDFLAGS=${LDFLAGS}
   NET_TEST_LIBS=${LIBS}

for i in \
      CC \
      MX_CFLAGS MX_INCS MX_LDFLAGS MX_LIBS \
      PS_DOTLOCK_CFLAGS PS_DOTLOCK_INCS PS_DOTLOCK_LDFLAGS PS_DOTLOCK_LIBS \
      NET_TEST_CFLAGS NET_TEST_INCS NET_TEST_LDFLAGS NET_TEST_LIBS \
      SU_CFLAGS SU_CXXFLAGS SU_INCS SU_LDFLAGS SU_LIBS \
      ; do
   eval j=\$${i}
   printf -- "${i} = ${j}\n" >> ${mk}
done

echo >> ${mk}

# mk-config.h (which becomes mx/gen-config.h)
${mv} ${h} ${tmp}
printf '#ifndef mx_GEN_CONFIG_H\n# define mx_GEN_CONFIG_H 1\n' > ${h}
${cat} ${tmp} >> ${h}
printf '\n#ifdef mx_SOURCE\n' >> ${h}

# Also need these for correct "second stage configuration changed" detection */
i=
if (${CC} --version) >/dev/null 2>&1; then
   i=`${CC} --version 2>&1 | ${awk} '
      BEGIN{l=""}
      {if(length($0)) {if(l) l = l "\\\\n"; l = l "@" $0}}
      END{gsub(/"/, "", l); print "\\\\n" l}
   '`
elif (${CC} -v) >/dev/null 2>&1; then
   i=`${CC} -v 2>&1 | ${awk} '
      BEGIN{l=""}
      {if(length($0)) {if(l) l = l "\\\\n"; l = l "@" $0}}
      END{gsub(/"/, "", l); print "\\\\n" l}
   '`
fi

CC=`squeeze_ws "${CC}"`
CFLAGS=`squeeze_ws "${CFLAGS}"`
LDLAGS=`squeeze_ws "${LDFLAGS}"`
LIBS=`squeeze_ws "${LIBS}"`
# $MAKEFLAGS often contain job-related things which hinders reproduceability.
# For at least GNU make(1) we can separate those and our regular configuration
# options by searching for the -- terminator
COMMLINE=`printf '%s\n' "${COMMLINE}" | ${sed} -e 's/.*--\(.*\)/\1/'`
COMMLINE=`squeeze_ws "${COMMLINE}"`

i=`printf '%s %s %s\n' "${CC}" "${CFLAGS}" "${i}"`
   printf '#define VAL_BUILD_CC "%s"\n' "$i" >> ${h}
   i=`string_to_char_array "${i}"`
   printf '#define VAL_BUILD_CC_ARRAY %s\n' "$i" >> ${h}
i=`printf '%s %s %s\n' "${CC}" "${LDFLAGS}" "${LIBS}"`
   printf '#define VAL_BUILD_LD "%s"\n' "$i" >> ${h}
   i=`string_to_char_array "${i}"`
   printf '#define VAL_BUILD_LD_ARRAY %s\n' "$i" >> ${h}
i=${COMMLINE}
   printf '#define VAL_BUILD_REST "%s"\n' "$i" >> ${h}
   i=`string_to_char_array "${i}"`
   printf '#define VAL_BUILD_REST_ARRAY %s\n' "$i" >> ${h}

# Throw away all temporaries
${rm} -rf ${tmp0}.* ${tmp0}*

# Create the string that is used by *features* and the version command.
# Take this nice opportunity and generate a visual listing of included and
# non-included features for the person who runs the configuration
echo 'The following features are included (+) or not (-):' > ${tmp}
set -- ${OPTIONS_DETECT} ${OPTIONS} ${OPTIONS_XTRA}
printf '/* The "feature string" */\n' >> ${h}
# Prefix sth. to avoid that + is expanded by *folder* (echo $features)
printf '#define VAL_FEATURES_CNT '${#}'\n#define VAL_FEATURES ",' >> ${h}
sep=
for opt
do
   sdoc=`option_doc_of ${opt}`
   [ -z "${sdoc}" ] && continue
   sopt="`echo ${opt} | ${tr} '[A-Z]_' '[a-z]-'`"
   feat_yes ${opt} && sign=+ || sign=-
   printf -- "${sep}${sign}${sopt}" >> ${h}
   sep=','
   printf ' %s %s: %s\n' ${sign} ${sopt} "${sdoc}" >> ${tmp}
done
# TODO instead of using sh+tr+awk+printf, use awk, drop option_doc_of, inc here
#exec 5>&1 >>${h}
#${awk} -v opts="${OPTIONS_DETECT} ${OPTIONS} ${OPTIONS_XTRA}" \
#   -v xopts="${XOPTIONS_DETECT} ${XOPTIONS} ${XOPTIONS_XTRA}" \
printf ',"\n' >> ${h}

# Create the real mk-config.mk
# Note we cannot use explicit ./ filename prefix for source and object
# pathnames because of a bug in bmake(1)
msg 'Creating object make rules'

if feat_yes DOTLOCK; then
   printf "OPTIONAL_PS_DOTLOCK = \$(VAL_PS_DOTLOCK)\n" >> ${mk}
   (cd "${SRCDIR}"; ${SHELL} ../mk/make-rules.sh ps-dotlock/*.c) >> ${mk}
else
   printf "OPTIONAL_PS_DOTLOCK =\n" >> ${mk}
fi

if feat_yes NET_TEST; then
   printf "OPTIONAL_NET_TEST = net-test\n" >> ${mk}
   (cd "${SRCDIR}"; ${SHELL} ../mk/make-rules.sh net-test/*.c) >> ${mk}
else
   printf "OPTIONAL_NET_TEST =\n" >> ${mk}
fi

# Not those SU sources with su_USECASE_MX_DISABLED
su_sources=`(
      cd "${SRCDIR}"
      for f in su/*.c; do
         ${grep} su_USECASE_MX_DISABLED "${f}" >/dev/null >&1 && continue
         echo ${f}
      done
      )`

mx_obj= su_obj=
if feat_no AMALGAMATION; then
   (cd "${SRCDIR}"; ${SHELL} ../mk/make-rules.sh ${su_sources}) >> ${mk}
   (cd "${SRCDIR}"; ${SHELL} ../mk/make-rules.sh mx/*.c) >> ${mk}
   mx_obj='$(MX_C_OBJ)' su_obj='$(SU_C_OBJ)'
else
   (cd "${SRCDIR}"; COUNT_MODE=0 ${SHELL} ../mk/make-rules.sh mx/*.c) >> ${mk}
   mx_obj=mx-main.o
   printf 'mx-main.o: gen-bltin-rc.h gen-mime-types.h' >> ${mk}

   printf '\n#endif /* mx_SOURCE */\n' >> ${h}
   printf '/* mx_HAVE_AMALGAMATION: include sources */\n' >> ${h}
   printf '#elif mx_GEN_CONFIG_H + 0 == 1\n' >> ${h}
   printf '# undef mx_GEN_CONFIG_H\n' >> ${h}
   printf '# define mx_GEN_CONFIG_H 2\n#ifdef mx_MASTER\n' >> ${h}

   for i in `printf '%s\n' ${su_sources} | ${sort}`; do
      printf '# include "%s%s"\n' "${SRCDIR}" "${i}" >> ${h}
   done
   echo >> ${mk}

   for i in `printf '%s\n' "${SRCDIR}"mx/*.c | ${sort}`; do
      i=`basename "${i}"`
      if [ "${i}" = main.c ]; then
         continue
      fi
      printf '# include "%s%s"\n' "${SRCDIR}mx/" "${i}" >> ${h}
   done
   echo >> ${mk}
fi
printf 'OBJ = %s\n' "${mx_obj} ${su_obj}" >> "${mk}"

printf '#endif /* mx_SOURCE */\n#endif /* mx_GEN_CONFIG_H */\n' >> ${h}

echo >> ${mk}
${cat} "${TOPDIR}"mk/make-config.in >> ${mk}

##
## Finished!
##

# We have completed the new configuration header.  Check whether *really*
# Do the "second stage configuration changed" detection, exit if nothing to do
if [ -f ${oldh} ]; then
   if ${cmp} ${h} ${oldh} >/dev/null 2>&1; then
      ${mv} -f ${oldh} ${h}
      msg 'Effective configuration is up-to-date'
      exit 0
   fi
   config_updated=1
   ${rm} -f ${oldh}
   msg 'Effective configuration has been updated..'
fi

if [ -n "${config_updated}" ]; then
   msg 'Wiping away old objects and such..'
   ( cd "${OBJDIR}"; oldmk=`${basename} ${oldmk}`; ${MAKE} -f ${oldmk} clean )
fi

# Ensure user edits in mx-config.h are incorporated, and that our generated
# mk-config.h becomes the new public mx/gen-config.h.
${cp} -f "${CWDDIR}"mx-config.h "${CWDDIR}"include/mx/config.h
${cp} -f ${h} "${CWDDIR}"include/mx/gen-config.h

msg ''
while read l; do msg "${l}"; done < ${tmp}

msg 'Setup:'
msg ' . System-wide resource file: %s/%s' \
   "${VAL_SYSCONFDIR}" "${VAL_SYSCONFRC}"
msg ' . bindir: %s' "${VAL_BINDIR}"
if feat_yes DOTLOCK; then
   msg ' . libexecdir: %s' "${VAL_LIBEXECDIR}"
fi
msg ' . mandir: %s' "${VAL_MANDIR}"
msg ' . M(ail)T(ransfer)A(gent): %s (argv0: %s)' \
   "${VAL_MTA}" "${VAL_MTA_ARGV0}"
msg ' . $MAIL spool directory: %s' "${VAL_MAIL}"
if feat_yes MAILCAP; then
   msg ' . Built-in $MAILCAPS path search: %s' "${VAL_MAILCAPS}"
fi

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
