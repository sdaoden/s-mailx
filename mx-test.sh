#!/bin/sh -
#@ See usage() below.
#
# Public Domain

: ${OBJDIR:=.obj}
: ${JOBNO:=}
: ${JOBWAIT:=42}
: ${JOBMON:=y}
: ${SKIPTEST:=}

# XXX LONG Note valgrind has problems with FDs in forked childs, which causes
# XXX UNTESTED some tests to fail (FD is rewound and thus will be dumped twice)
MEMTESTER= #'valgrind --leak-check=full --log-file=.vl-%p '

export \
   OBJDIR JOBNO JOBWAIT JOBMON SKIPTEST \
   MEMTESTER

##  --  >8  --  8<  --  ##

# environ,usage,argv {{{
LC_ALL=C LANG=C TZ=UTC
export LC_ALL LANG TZ

usage() {
   cat >&2 <<'_EOT'
Synopsis: [OBJDIR=x] mx-test.sh [--no-colour] --check mailx-binary [:SKIPTEST:]
Synopsis: [OBJDIR=x] mx-test.sh [--no-colour] --run-test mailx-binary [:TEST:]
Synopsis: [OBJDIR=x] mx-test.sh [--no-colour]

 --check EXE [:SKIPTEST:] run test series, exit success or error.
                          [:SKIPTEST:]s (and $SKIPTEST=) will be excluded.
 --run-test EXE [:TEST:]  run all or only the given TESTs, and create
                          test output data files; if run in a git(1)
                          checkout with the [test-out] branch available,
                          it will also create file diff(1)erences
 --no-colour              or $MAILX_CC_TEST_NO_COLOUR: no colour
                          (for example to: grep ^ERROR)
                          $MAILX_CC_ALL_TESTS_DUMPERR in addition for even
                          easier grep ^ERROR handling

EXE is either an absolute path or interpreted relative to $OBJDIR.
$JOBNO could denote number of parallel jobs, $JOBWAIT a timeout, and
$JOBMON controls usage of "set -m".  $MAILX_CC_TEST_NO_CLEANUP skips deletion
of test data (works only with one test, aka --run-test), $MAILX_TEST_KEEP_DATA
always works, it keeps only the output files.
_EOT
   exit 1
}

if [ -z "${MAILX__CC_TEST_RUNNING}" ]; then
   CHECK= RUN_TEST= MAILX= NOCOLOUR=

   while [ ${#} -gt 0 ]; do
      if [ "${1}" = --no-colour ]; then
         NOCOLOUR=y
         shift
      elif [ "${1}" = -h ] || [ "${1}" = --help ]; then
         usage
         exit 0
      else
         break
      fi
   done

   if [ "${1}" = --check ]; then
      CHECK=1 MAILX=${2}
      shift 2
      SKIPTEST="${@} ${SKIPTEST}"
      echo 'Mode: --check, binary: '"${MAILX}"
   elif [ "${1}" = --run-test ]; then
      [ ${#} -ge 2 ] || usage
      RUN_TEST=1 MAILX=${2}
      shift 2
      echo 'Mode: --run-test, binary: '"${MAILX}"
   else
      [ ${#} -eq 0 ] || usage
      echo 'Mode: full compile test, this will take a long time...'
      MAILX__CC_TEST_NO_DATA_FILES=1
      export MAILX__CC_TEST_NO_DATA_FILES
   fi

   export CHECK RUN_TEST MAILX NOCOLOUR
fi
# }}}

# Config {{{
# Instead of figuring out the environment in here, require a configured build
# system and include that!  Our makefile and configure ensure that this test
# does not run in the configured, but the user environment nonetheless!

i=
while :; do
   if [ -f ./mk-config.env ]; then
      break
   elif [ -f snailmail.jpg ] && [ -f "${OBJDIR}"/mk-config.env ]; then
      i=`pwd`/ # not from environment, sic
      cd "${OBJDIR}"
      break
   else
      echo >&2 'S-nail/S-mailx is not configured.'
      echo >&2 'This test script requires the shell environment that only the'
      echo >&2 'configuration script can figure out, even if used to test'
      echo >&2 'a different binary than the one that would be produced!'
      echo >&2 '(The information will be in ${OBJDIR:=.obj}/mk-config.env.)'
      echo >&2 'Hit RETURN to run "make config"'
      read l
      make config
   fi
done
. ./mk-config.env
if [ -z "${MAILX__CC_TEST_RUNNING}" ]; then
   MAILX__CC_TEST_RUNNING=y
   export MAILX__CC_TEST_RUNNING
   exec "${SHELL}" "${i}${0}" "${@}"
fi

# We need *stealthmua* regardless of $SOURCE_DATE_EPOCH, the program name as
# such is a compile-time variable
ARGS='-:/ -Sdotlock-disable -Smta=test -Smta-bcc-ok -Smemdebug -Sstealthmua'
   ARGS="${ARGS}"' -Smime-encoding=quoted-printable -Snosave'
   ARGS="${ARGS}"' -Smailcap-disable -Smimetypes-load-control='
NOBATCH_ARGS="${ARGS}"' -Sexpandaddr'
   ARGS="${ARGS}"' -Sexpandaddr=restrict -#'
ADDARG_UNI=-Sttycharset=UTF-8
CONF=../make.rc
# XXX BODY,MBOX,ERR: leading dot reminiscent
BODY=./.cc-body.txt
MBOX=./.cc-test.mbox
ERR=./.cc-test.err # Covers some which cannot be checksummed; not quoted!
MAIL=/dev/null
#UTF8_LOCALE= HONOURS_READONLY= autodetected unless set
TMPDIR=`${pwd}`

export ARGS NOBATCH_ARGS ADDARG_UNI CONF BODY MBOX ERR MAIL TMPDIR

# When testing mass mail/loops, maximum number of recipients/loops.
# TODO note we do not gracefully handle ARG_MAX excess yet!
# Those which use this have checksums for 2001 and 201.
# Some use the smaller automatically if +debug
LOOPS_BIG=2001 LOOPS_SMALL=201
LOOPS_MAX=$LOOPS_SMALL
# }}}

## Now it is getting really weird. You have been warned.

# Setup and support {{{

# Wed Oct  2 01:50:07 UTC 1996
SOURCE_DATE_EPOCH=844221007
export SOURCE_DATE_EPOCH

unset POSIXLY_CORRECT LOGNAME USER

# Since we invoke $MAILX from within several directories we need a fully
# qualified path.  Or at least something similar.
{ echo ${MAILX} | ${grep} -q ^/; } || MAILX="${TMPDIR}"/${MAILX}
RAWMAILX=${MAILX}

# "sh -c -- 'echo yes'" must echo "yes"; FreeBSD #264319, #220587: work around
if [ "`\"${VAL_SHELL}\" -c -- 'echo yes'`" = yes ]; then
   T_MAILX= T_SH=
else
   echo '! '"${VAL_SHELL}"' cannot deal with "-c -- ARG", using workaround'
   T_MAILX=./t.mailx.sh T_SH=./t.sh.sh
   ${rm} -f ${T_MAILX} ${T_SH}
   ${cat} > ${T_MAILX} <<_EOT
#!${VAL_SHELL}
SHELL=${TMPDIR}/${T_SH}
export SHELL
exec ${MAILX} "\$@"
_EOT
   ${cat} > ${T_SH} <<_EOT
#!${VAL_SHELL}
shift 2
exec ${VAL_SHELL} -c "\${@}"
_EOT
   ${chmod} 0755 ${T_MAILX} ${T_SH}
   MAILX=${TMPDIR}/${T_MAILX}
fi

[ -x "${MAILX}" ] || usage
MAILX="${MEMTESTER}${MAILX}"
export RAWMAILX MAILX

GIT_REPO=
[ -d ../.git ] && [ -z "${MAILX__CC_TEST_NO_DATA_FILES}" ] && GIT_REPO=1

# We want an UTF-8 locale, and HONOURS_READONLY {{{
if [ -n "${CHECK}${RUN_TEST}" ]; then
   if [ -z "${UTF8_LOCALE}" ]; then
      # Try ourselfs via nl_langinfo(CODESET) first (requires a new version)
      if command -v "${RAWMAILX}" >/dev/null 2>&1 &&
            (</dev/null ${RAWMAILX} -:/ -Xxit) >/dev/null 2>&1; then
         echo 'Trying to detect UTF-8 locale via '"${RAWMAILX}"
         # C,POSIX last due to faulty localedef(1) result of GNU C lib 2.3[24]
         # so here political friction for some decades, too
         i=`</dev/null LC_ALL=de_DE.utf8 ${RAWMAILX} ${ARGS} -X '
            \define cset_test {
               \if "${ttycharset}" =%?case utf
                  \echo $LC_ALL
                  \xit 0
               \end
               \if ${#} -gt 0
                  \set LC_ALL=${1}
                  \shift
                  \xcall cset_test "${@}"
               \end
               \xit 1
            }
            \call cset_test \
               de_DE.UTF-8 \
               zh_CN.utf8 zh_CN.UTF-8 \
               ru_RU.utf8 ru_RU.UTF-8 \
               en_GB.utf8 en_GB.UTF-8 en_US.utf8 en_US.UTF-8 \
               POSIX.utf8 POSIX.UTF-8 \
               C.utf8 C.UTF-8
         '`
         [ $? -eq 0 ] && UTF8_LOCALE=$i
      fi

      if [ -z "${UTF8_LOCALE}" ] && (locale yesexpr) >/dev/null 2>&1; then
         echo 'Trying to detect UTF-8 locale via locale -a'
         UTF8_LOCALE=`locale -a | { m=
            while read n; do
               if { echo ${n} |
                     ${grep} -i -e utf8 -e utf-8; } >/dev/null 2>&1; then
                  m=${n}
                  if { echo ${n} |
                        ${grep} -e POSIX -e en_EN -e en_US; } \
                           >/dev/null 2>&1; then
                     break
                  fi
               fi
            done
            echo ${m}
         }`
      fi
   fi

   if [ -n "${UTF8_LOCALE}" ]; then
      echo 'Using Unicode locale '${UTF8_LOCALE}
   else
      echo 'No Unicode locale found, disabling Unicode tests'
   fi

   if [ -z "${HONOURS_READONLY}" ]; then
      trap "${rm} -f ./.tisrdonly" EXIT
      trap "exit 1" HUP INT TERM
      printf '' > ./.tisrdonly
      ${chmod} 0444 ./.tisrdonly
      if (printf 'no\n' > ./.tisrdonly) >/dev/null 2>&1 &&
            test -s ./.tisrdonly; then
         HONOURS_READONLY=
      else
         HONOURS_READONLY=yes
      fi
      ${rm} -f ./.tisrdonly
      trap '' EXIT HUP INT TERM
   fi
fi

export UTF8_LOCALE HONOURS_READONLY
# }}}

DEVELDIFF= DUMPERR=
TESTS_PERFORMED=0 TESTS_OK=0 TESTS_FAILED=0 TESTS_SKIPPED=0
JOBS=0 JOBLIST= JOBREAPER= JOBSYNC=
SUBSECOND_SLEEP=
   ( sleep .1 ) >/dev/null 2>&1 && SUBSECOND_SLEEP=y

   TESTS_NET_TEST=
   [ "${OPT_NET_TEST}" = 1 ] && [ -x ./net-test ] && TESTS_NET_TEST=1
   export TESTS_NET_TEST

COLOR_ERR_ON= COLOR_ERR_OFF=  COLOR_DBGERR_ON= COLOR_DBGERR_OFF=
COLOR_WARN_ON= COLOR_WARN_OFF=
COLOR_OK_ON= COLOR_OK_OFF=
ESTAT=0
TEST_NAME=

${rm} -rf ./t.*.d ./t.*.io ./t.*.result ./t.time.out ./t.tls.db
trap "
   jobreaper_stop
   [ -z "${MAILX_CC_TEST_NO_CLEANUP}" ] &&
      ${rm} -rf ./t.*.d ./t.*.io ./t.*.result \
         ./t.time.out ./t.tls.db ${T_MAILX} ${T_SH}
" EXIT
trap "exit 1" HUP INT QUIT TERM

# JOBS {{{
jobs_max() { :; }

if [ -n "${JOBNO}" ]; then
   if { echo ${JOBNO} | grep -q -e '^[0-9]\{1,\}$'; }; then :; else
      echo >&2 '$JOBNO='${JOBNO}' is not a valid number, using 1'
      JOBNO=1
   fi
else
   jobs_max() {
      # The user desired variant
      if ( echo "${MAKEFLAGS}" | ${grep} -- -j ) >/dev/null 2>&1; then
         i=`echo "${MAKEFLAGS}" |
               ${sed} -e 's/^.*-j[ 	]*\([0-9]\{1,\}\).*$/\1/'`
         if ( echo "${i}" | grep -q -e '^[0-9]\{1,\}$' ); then
            printf 'Job number derived from MAKEFLAGS: %s\n' ${i}
            JOBNO=${i}
            [ "${JOBNO}" -eq 0 ] && JOBNO=1
            return
         fi
      fi

      # The actual hardware
      printf 'all:\n' > t.mk.io
      if ( ${MAKE} -j 10 -f t.mk.io ) >/dev/null 2>&1; then
         if command -v nproc >/dev/null 2>&1; then
            i=`nproc 2>/dev/null`
            [ ${?} -eq 0 ] && JOBNO=${i}
         else
            i=`getconf _NPROCESSORS_ONLN 2>/dev/null`
            j=${?}
            if [ ${j} -ne 0 ]; then
               i=`getconf NPROCESSORS_ONLN 2>/dev/null`
               j=${?}
            fi
            if [ ${j} -ne 0 ]; then
               # SunOS 5.9 ++
               if command -v kstat >/dev/null 2>&1; then
                  i=`PERL5OPT= kstat -p cpu | ${awk} '
                     BEGIN{no=0; FS=":"}
                     {if($2 > no) max = $2; next}
                     END{print ++max}
                     ' 2>/dev/null`
                  j=${?}
               fi
            fi
            if [ ${j} -eq 0 ] && [ -n "${i}" ]; then
               printf 'Job number derived from CPU number: %s\n' ${i}
               JOBNO=${i}
            fi
         fi
         [ "${JOBNO}" -eq 0 ] && JOBNO=1
      fi
   }
fi

jobreaper_start() {
   case "${JOBMON}" in
   [yY]*)
      # There were problems when using monitor mode with mksh
      i=`env -i ${SHELL} -c 'echo $KSH_VERSION'`
      if [ -n "${i}" ]; then
         if [ "${i}" != "${i#*MIRBSD}" ]; then
            JOBMON=
         fi
      fi

      if [ -n "${JOBMON}" ]; then
         ( set -m ) </dev/null >/dev/null 2>&1 || JOBMON=
      else
         printf >&2 '%s! $JOBMON: $SHELL %s incapable, disabled!%s\n' \
            "${COLOR_ERR_ON}" "${SHELL}" "${COLOR_ERR_OFF}"
         printf >&2 '%s!  No process groups available, killed tests may '\
'leave process "zombies"!%s\n' \
            "${COLOR_ERR_ON}" "${COLOR_ERR_OFF}"
      fi
      ;;
   *)
      JOBMON=
      ;;
   esac
}

jobreaper_stop() {
   if [ ${JOBS} -gt 0 ]; then
      echo 'Cleaning up running jobs'
      [ -n "${JOBREAPER}" ] && kill -KILL ${JOBREAPER} >/dev/null 2>&1
      jtimeout
      wait ${JOBLIST}
      JOBLIST=
   fi
}

jspawn() {
   if [ -n "${CHECK}" ] && [ -n "${SKIPTEST}" ]; then
      i="${@}"
      j="${1}"
      k=
      set -- ${SKIPTEST}
      SKIPTEST=
      while [ ${#} -gt 0 ]; do
         if [ "${1}" != "${j}" ]; then
            SKIPTEST="${SKIPTEST} ${1}"
         elif [ -z "${k}" ]; then
            k=y
            [ ${JOBNO} -gt 1 ] && printf ' '
            t_echoskip ${1}
         fi
         shift
      done
      [ -n "${k}" ] && return
      set -- "${i}"
   fi

   if [ ${JOBNO} -gt 1 ]; then
      # We are spawning multiple jobs..
      [ ${JOBS} -eq 0 ] && printf '...'
      JOBS=`add ${JOBS} 1`
      printf ' [%s=%s]' ${JOBS} "${1}"
   else
      JOBS=1
      # Assume problems exist, do not let user keep hanging on terminal
      if [ -n "${RUN_TEST}" ]; then
         printf '... [%s]\n' "${1}"
      fi
   fi

   [ -n "${JOBMON}" ] && set -m >/dev/null 2>&1
   (  # Place the job in its own directory to ease file management
      trap '' EXIT HUP INT QUIT TERM USR1 USR2
      ${mkdir} t.${JOBS}.d && cd t.${JOBS}.d &&
         eval t_${1} ${JOBS} ${1} &&
         ${rm} -f ../t.${JOBS}.id
   ) > t.${JOBS}.io </dev/null & # 2>&1 </dev/null &
   i=${!}
   [ -n "${JOBMON}" ] && set +m >/dev/null 2>&1
   JOBLIST="${JOBLIST} ${i}"
   printf '%s\n%s\n' ${i} ${1} > t.${JOBS}.id

   # ..until we should sync or reach the maximum concurrent number
   [ ${JOBS} -lt ${JOBNO} ] && return

   jsync 1
}

jsync() {
   if [ ${JOBS} -eq 0 ]; then
      [ -n "${TEST_ANY}" ] && printf '\n'
      TEST_ANY=
      return
   fi
   [ -z "${JOBSYNC}" ] && [ ${#} -eq 0 ] && return

   [ ${JOBNO} -ne 1 ] && printf ' .. waiting\n'

   # Start an asynchronous notify process
   ${rm} -f ./t.time.out
   (
      sleep ${JOBWAIT} &
      sleeper=${!}
      trap "kill -TERM ${sleeper}; exit 1" HUP INT TERM
      wait ${sleeper}
      trap '' HUP INT TERM
      printf '' > ./t.time.out
   ) </dev/null >/dev/null 2>&1 &
   JOBREAPER=${!}

   # Then loop a while, looking out for collecting tests
   loops=0
   while :; do
      [ -f ./t.time.out ] && break
      alldone=1
      i=0
      while [ ${i} -lt ${JOBS} ]; do
         i=`add ${i} 1`
         [ -f t.${i}.id ] || continue
         alldone=
         break
      done
      [ -n "${alldone}" ] && break

      if [ -z "${SUBSECOND_SLEEP}" ]; then
         loops=`add ${loops} 1`
         [ ${loops} -lt 111 ] && continue
         sleep 1 &
      else
         sleep .25 &
      fi
      wait ${!}
   done

   if [ -f ./t.time.out ]; then
      ${rm} -f ./t.time.out
      jtimeout
   else
      kill -TERM ${JOBREAPER} >/dev/null 2>&1
   fi
   wait ${JOBREAPER}
   JOBREAPER=

   # Now collect the zombies
   wait ${JOBLIST}
   JOBLIST=

   # Update global counters
   i=0
   while [ ${i} -lt ${JOBS} ]; do
      i=`add ${i} 1`

      [ -s t.${i}.io ] && ${cat} t.${i}.io
      if [ -n "${DUMPERR}" ] && [ -s ./t.${i}.d/${ERR} ]; then
         printf '%s   [Debug/Devel: nullified errors]\n' "${COLOR_DBGERR_ON}"
         while read l; do
            printf '   %s\n' "${l}"
         done < t.${i}.d/${ERR}
         printf '%s' "${COLOR_DBGERR_OFF}"
      fi

      if [ -f t.${i}.id ]; then
         { read pid; read desc; } < t.${i}.id
         desc=${desc#${desc%%[! ]*}}
         desc=${desc%${desc##*[! ]}}
         [ -s t.${i}.io ] && printf >&2 '\n'
         printf >&2 '%s!! Timeout: reaped job %s [%s]%s\n' \
            "${COLOR_ERR_ON}" ${i} "${desc}" "${COLOR_ERR_OFF}"
         TESTS_FAILED=`add ${TESTS_FAILED} 1`
      elif [ -s t.${i}.result ]; then
         read es tp to tf ts < t.${i}.result
         TESTS_PERFORMED=`add ${TESTS_PERFORMED} ${tp}`
         TESTS_OK=`add ${TESTS_OK} ${to}`
         TESTS_FAILED=`add ${TESTS_FAILED} ${tf}`
         TESTS_SKIPPED=`add ${TESTS_SKIPPED} ${ts}`
         [ "${es}" != 0 ] && ESTAT=${es}
      else
         TESTS_FAILED=`add ${TESTS_FAILED} 1`
         ESTAT=1
      fi
   done

   [ -z "${MAILX_CC_TEST_NO_CLEANUP}" ] &&
      ${rm} -rf ./t.*.d ./t.*.id ./t.*.io t.*.result ./t.time.out

   JOBS=0
}

jtimeout() {
   i=0
   while [ ${i} -lt ${JOBS} ]; do
      i=`add ${i} 1`
      if [ -f t.${i}.id ] &&
            read pid < t.${i}.id >/dev/null 2>&1 &&
            kill -0 ${pid} >/dev/null 2>&1; then
         j=${pid}
         [ -n "${JOBMON}" ] && j=-${j}
         kill -KILL ${j} >/dev/null 2>&1
      else
         ${rm} -f t.${i}.id
      fi
   done
}
# }}}

# echoes, checks, color_init, add, modulo {{{
t_prolog() {
   shift

   ESTAT=0 TESTS_PERFORMED=0 TESTS_OK=0 TESTS_FAILED=0 TESTS_SKIPPED=0 \
      TEST_NAME=${1} TEST_ANY=

   printf '%s[%s]%s\n' "" "${TEST_NAME}" ""
}

t_epilog() {
   [ -n "${TEST_ANY}" ] && printf '\n'

   printf '%s %s %s %s %s\n' \
      ${ESTAT} \
         ${TESTS_PERFORMED} ${TESTS_OK} ${TESTS_FAILED} ${TESTS_SKIPPED} \
      > ../t.${1}.result
}

t_echo() {
   [ -n "${TEST_ANY}" ] && __i__=' ' || __i__=
   printf "${__i__}"'%s' "${*}"
   TEST_ANY=1
}

t_echook() {
   [ -n "${TEST_ANY}" ] && __i__=' ' || __i__=
   printf "${__i__}"'%s%s:ok%s' "${COLOR_OK_ON}" "${*}" "${COLOR_OK_OFF}"
   TEST_ANY=1
}

t_echoerr() {
   ESTAT=1
   t_echo0err "${@}"
}

t_echo0err() {
   [ -n "${TEST_ANY}" ] && __i__="\n" || __i__=
   printf "${__i__}"'%sERROR: %s%s\n' \
      "${COLOR_ERR_ON}" "${*}" "${COLOR_ERR_OFF}"
   TEST_ANY=
}

t_echowarn() {
   [ -n "${TEST_ANY}" ] && __i__=' ' || __i__=
   printf "${__i__}"'%s%s%s' "${COLOR_WARN_ON}" "${*}" "${COLOR_WARN_OFF}"
   TEST_ANY=1
}

t_echoskip() {
   [ -n "${TEST_ANY}" ] && __i__=' ' || __i__=
   printf "${__i__}"'%s%s[skip]%s' \
      "${COLOR_WARN_ON}" "${*}" "${COLOR_WARN_OFF}"
   TEST_ANY=1
   TESTS_SKIPPED=`add ${TESTS_SKIPPED} 1`
}

check() {
   restat=${?} tid=${1} eestat=${2} f=${3} s=${4} optmode=${5}

   TESTS_PERFORMED=`add ${TESTS_PERFORMED} 1`

   case "${optmode}" in
   '') ;;
   async)
      [ "$eestat" = - ] || exit 200
      while :; do
         [ -f "${f}" ] && break
         t_echowarn "[${tid}:async=wait]"
         sleep 1 &
         wait ${!}
      done
      ;;
   *) exit 222;;
   esac

   check__bad= check__runx=

   if [ "${eestat}" != - ] && [ "${restat}" != "${eestat}" ]; then
      ESTAT=1
      t_echoerr "${tid}: bad-status: ${restat} != ${eestat}"
      check__bad=1
   fi

   csum="`${cksum} < "${f}" | ${sed} -e 's/[ 	]\{1,\}/ /g'`"
   if [ "${csum}" = "${s}" ]; then
      t_echook "${tid}"
      check__runx=${DEVELDIFF}
   else
      ESTAT=1
      t_echoerr "${tid}: checksum mismatch (got ${csum})"
      check__bad=1 check__runx=1
   fi

   if [ -z "${check__bad}" ]; then
      TESTS_OK=`add ${TESTS_OK} 1`
   else
      TESTS_FAILED=`add ${TESTS_FAILED} 1`
   fi

   if [ -n "${CHECK}${RUN_TEST}" ]; then
      x="t.${TEST_NAME}-${tid}"
      if [ -n "${RUN_TEST}" ] ||
            [ -n "${check__runx}" -a -n "${GIT_REPO}" ]; then
         ${cp} -f "${f}" ../"${x}"
      fi

      if [ -n "${check__runx}" ] && [ -n "${GIT_REPO}" ] &&
            command -v git >/dev/null 2>&1 &&
            command -v diff >/dev/null 2>&1; then
         y=test-out
         if (GIT_CONFIG=/dev/null git rev-parse --verify $y
               ) >/dev/null 2>&1; then :; else
            y=refs/remotes/origin/test-out
            (GIT_CONFIG=/dev/null git rev-parse --verify $y
               ) >/dev/null 2>&1 || y=
         fi
         if [ -n "${y}" ]; then
            if GIT_CONFIG=/dev/null git show "${y}":"${x}" > \
                  ../"${x}".old 2>/dev/null; then
               diff -ru ../"${x}".old ../"${x}" > ../"${x}".diff
               if [ ${?} -eq 0 ]; then
                  if [ -z "${MAILX_CC_TEST_NO_CLEANUP}" ] &&
                        [ -z "${MAILX_TEST_KEEP_DATA}" ]; then
                     ${rm} -f ../"${x}" ../"${x}".old ../"${x}".diff
                  fi
               elif [ -n "${MAILX_CC_ALL_TESTS_DUMPERR}" ]; then
                  while read l; do
                     printf 'ERROR-DIFF  %s\n' "${l}"
                  done < ../"${x}".diff
               fi
            else
               t_echo0err "${tid}: misses [test-out] template"
            fi
         fi
      fi
   fi
}

check_ex0() {
   # $1=test name [$2=status]
   __qm__=${?}
   [ ${#} -gt 1 ] && __qm__=${2}

   TESTS_PERFORMED=`add ${TESTS_PERFORMED} 1`

   if [ ${__qm__} -ne 0 ]; then
      ESTAT=1
      t_echoerr "${1}: unexpected non-0 exit status: ${__qm__}"
      TESTS_FAILED=`add ${TESTS_FAILED} 1`
   else
      t_echook "${1}"
      TESTS_OK=`add ${TESTS_OK} 1`
   fi
}

check_exn0() {
   # $1=test name [$2=status]
   __qm__=${?}
   [ ${#} -gt 1 ] && __qm__=${2}
   [ ${#} -gt 2 ] && __expect__=${3} || __expect__=

   TESTS_PERFORMED=`add ${TESTS_PERFORMED} 1`

   if [ ${__qm__} -eq 0 ]; then
      ESTAT=1
      t_echoerr "${1}: unexpected 0 exit status: ${__qm__}"
      TESTS_FAILED=`add ${TESTS_FAILED} 1`
   elif [ -n "${__expect__}" ] && [ ${__expect__} -ne ${__qm__} ]; then
      ESTAT=1
      t_echoerr "${1}: unexpected exit status: ${__qm__} != ${__expected__}"
      TESTS_FAILED=`add ${TESTS_FAILED} 1`
   else
      t_echook "${1}"
      TESTS_OK=`add ${TESTS_OK} 1`
   fi
}

color_init() {
   [ -n "${NOCOLOUR}" ] && return
   [ -n "${MAILX_CC_TEST_NO_COLOUR}" ] && return
   # We do not want color for "make test > .LOG"!
   if [ -t 1 ] && command -v tput >/dev/null 2>&1; then
      { sgr0=`tput sgr0`; } 2>/dev/null
      [ $? -eq 0 ] || return
      { saf1=`tput setaf 1`; } 2>/dev/null
      [ $? -eq 0 ] || return
      { saf2=`tput setaf 2`; } 2>/dev/null
      [ $? -eq 0 ] || return
      { saf3=`tput setaf 3`; } 2>/dev/null
      [ $? -eq 0 ] || return
      { saf5=`tput setaf 5`; } 2>/dev/null
      [ $? -eq 0 ] || return
      { b=`tput bold`; } 2>/dev/null
      [ $? -eq 0 ] || return

      COLOR_ERR_ON=${saf1}${b} COLOR_ERR_OFF=${sgr0}
      COLOR_DBGERR_ON=${saf5} COLOR_DBGERR_OFF=${sgr0}
      COLOR_WARN_ON=${saf3}${b} COLOR_WARN_OFF=${sgr0}
      COLOR_OK_ON=${saf2} COLOR_OK_OFF=${sgr0}
      unset saf1 saf2 saf3 b
   fi
}

if ( [ "$((1 + 1))" = 2 ] ) >/dev/null 2>&1; then
   add() {
      echo "$((${1} + ${2}))"
   }
else
   add() {
      ${awk} 'BEGIN{print '${1}' + '${2}'}'
   }
fi

if ( [ "$((2 % 3))" = 2 ] ) >/dev/null 2>&1; then
   modulo() {
      echo "$((${1} % ${2}))"
   }
else
   modulo() {
      ${awk} 'BEGIN{print '${1}' % '${2}'}'
   }
fi

have_feat() {
   ${RAWMAILX} ${ARGS} -X '
      \if ${features} =% ,+'"${1}"',
         \xit 0
      \endif
      \xit 1
   '
}
# }}}
# }}}

# Absolute Basics {{{
t_eval() { # {{{
   t_prolog "${@}"

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1
set i=du
echo 1:
echo $i
echo '$i'
eval echo '$i'
echo 2:
echo "\"'$i'\""
eval echo "\"'$i'\""
eval eval echo "\"'$i'\""
eval eval eval eval echo "\"'$i'\""
	__EOT

   check 1 0 ./t1 '847277817 33'

   t_epilog "${@}"
} # }}}

t_call() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1
	define one {
	   echo one<$0>: $#: $*
	}
	call one
	call one 1
	call one 1 2
	call one 1 2 3
	define two {
	   echo two<$0>: $#: $*
	   call one "$@"
	}
	call two
	call two a
	call two a b
	call two a b c
	define three {
	   echo three<$0>: $#: $*
	   call two "$@"
	}
	call three
	call three not
	call three not my
	call three not my love
	__EOT
   #}}}
   check 1 0 ./t1 '59079195 403'

   t_epilog "${@}"
} # }}}

t_X_Y_opt_input_go_stack() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' > ./t.rc
	echo 1
	define mac0 {
	   echo mac0-1 via1 $0
	}
	call mac0
	echo 2
	source '\
	   echo "define mac1 {";\
	   echo "  echo mac1-1 via1 \$0";\
	   echo "  call mac0";\
	   echo "  echo mac1-2";\
	   echo "  call mac2";\
	   echo "  echo mac1-3";\
	   echo "}";\
	   echo "echo 1-1";\
	   echo "define mac2 {";\
	   echo "  echo mac2-1 via1 \$0";\
	   echo "  call mac0";\
	   echo "  echo mac2-2";\
	   echo "}";\
	   echo "echo 1-2";\
	   echo "call mac1";\
	   echo "echo 1-3";\
	   echo "source \"\
	      echo echo 1-1-1 via1 \$0;\
	      echo call mac0;\
	      echo echo 1-1-2;\
	   | \"";\
	   echo "echo 1-4";\
	|  '
	echo 3
	call mac2
	echo 4
	undefine *
	__EOT
   #}}}

   # The -X option supports multiline arguments, and those can internally use
   # reverse solidus newline escaping.  And all -X options are joined...
   #{{{
   APO=\'
   < ./t.rc ${MAILX} ${ARGS} \
      -X 'e\' \
      -X ' c\' \
      -X '  h\' \
      -X '   o \' \
      -X 1 \
      -X'
   define mac0 {
      echo mac0-1 via2 $0
   }
   call mac0
   echo 2
   ' \
      -X'
   source '${APO}'\
      echo "define mac1 {";\
      echo "  echo mac1-1 via2 \$0";\
      echo "  call mac0";\
      echo "  echo mac1-2";\
      echo "  call mac2";\
      echo "  echo mac1-3";\
      echo "}";\
      echo "echo 1-1";\
      echo "define mac2 {";\
      echo "  echo mac2-1 via2 \$0";\
      echo "  call mac0";\
      echo "  echo mac2-2";\
      echo "}";\
      echo "echo 1-2";\
      echo "call mac1";\
      echo "echo 1-3";\
      echo "source \"\
         echo echo 1-1-1 via2 \$0;\
         echo call mac0;\
         echo echo 1-1-2;\
      | \"";\
      echo "echo 1-4";\
   |  '${APO}'
   echo 3
   ' \
      -X'
   call mac2
   echo 4
   undefine *
   ' > ./t1
   #}}}
   check 1 0 ./t1 '1786542668 416'

   # The -Y option supports multiline arguments, and those can internally use
   # reverse solidus newline escaping.
   #{{{
   APO=\'
   < ./t.rc ${MAILX} ${ARGS} \
      -X 'echo FIRST_X' \
      -X 'echo SECOND_X' \
      -Y 'e\' \
      -Y ' c\' \
      -Y '  h\' \
      -Y '   o \' \
      -Y 1 \
      -Y'
   define mac0 {
      echo mac0-1 via2 $0
   }
   call mac0
   echo 2
   ' \
      -Y'
   source '${APO}'\
      echo "define mac1 {";\
      echo "  echo mac1-1 via2 \$0";\
      echo "  call mac0";\
      echo "  echo mac1-2";\
      echo "  call mac2";\
      echo "  echo mac1-3";\
      echo "}";\
      echo "echo 1-1";\
      echo "define mac2 {";\
      echo "  echo mac2-1 via2 \$0";\
      echo "  call mac0";\
      echo "  echo mac2-2";\
      echo "}";\
      echo "echo 1-2";\
      echo "call mac1";\
      echo "echo 1-3";\
      echo "source \"\
         echo echo 1-1-1 via2 \$0;\
         echo call mac0;\
         echo echo 1-1-2;\
      | \"";\
      echo "echo 1-4";\
   |  '${APO}'
   echo 3
   ' \
      -Y'
   call mac2
   echo 4
   undefine *
   ' \
      -Y 'echo LAST_Y' > ./t2
   #}}}
   check 2 0 ./t2 '1845176711 440'

   # Compose mode, too!
   </dev/null ${MAILX} ${ARGS} \
      -X 'echo X before compose mode' \
      -Y '~s Subject via -Y' \
      -Y 'Body via -Y' -. ./t.mbox > ./t4 2>&1
   check 3 0 ./t.mbox '264636255 125'
   check 4 - ./t4 '467429373 22'

   ${cat} <<-_EOT | ${MAILX} ${ARGS} -t \
      -X 'echo X before compose mode' \
      -Y '~s Subject via -Y' \
      -Y 'Additional body via -Y' -. ./t.mbox > ./t6 2>&1
	from: heya@exam.ple
	subject:diet not to be seen!

	this body via -t.
	_EOT
   check 5 0 ./t.mbox '641630721 321'
   check 6 - ./t6 '467429373 22'

   #
   printf 'this body via stdin pipe.\n' | ${MAILX} ${NOBATCH_ARGS} \
      -X 'echo X before compose mode' \
      -Y '~s Subject via -Y (not!)' \
      -Y 'Additional body via -Y, nobatch mode' -. ./t.mbox > ./t8 2>&1
   check 7 0 ./t.mbox '3953748270 498'
   check 8 - ./t8 '467429373 22'

   printf 'this body via stdin pipe.\n' | ${MAILX} ${ARGS} \
      -X 'echo X before compose mode' \
      -Y '~s Subject via -Y' \
      -Y 'Additional body via -Y, batch mode' -. ./t.mbox > ./t10 2>&1
   check 9 0 ./t.mbox '2012913894 672'
   check 10 - ./t10 '467429373 22'

   # Test for [8412796a] (n_cmd_arg_parse(): FIX token error -> crash, e.g.
   # "-RX 'bind;echo $?' -Xx".., 2018-08-02)
   ${MAILX} ${ARGS} -RX'call;echo $?' -Xx > ./tcmdline 2>&1
   ${MAILX} ${ARGS} -RX'call ;echo $?' -Xx >> ./tcmdline 2>&1
   ${MAILX} ${ARGS} -RX'call	;echo $?' -Xx >> ./tcmdline 2>&1
   ${MAILX} ${ARGS} -RX'call      ;echo $?' -Xx >> ./tcmdline 2>&1
   check cmdline 0 ./tcmdline '1867586969 8'

   #{{{ Recursion via `source'
   ${cat} > ./t11-1.rc <<- '_EOT'; \
      ${cat} > ./t11-2.rc <<- '_EOT'; \
      ${cat} > ./t11-3.rc <<- '_EOT'; \
      ${cat} > ./t11-4.rc <<- '_EOT'
	define r1 {
		echo r1: $*
		source ./t11-2.rc
		eval $1 r2 "$@"
	}
	_EOT
	define r2 {
		echo r2: $*
		source ./t11-3.rc
		eval $1 r3 "$@"
	}
	_EOT
	define r3 {
		echo r3: $*
		source ./t11-4.rc
		eval $1 r4 "$@"
	}
	_EOT
	define r4 {
		echo r4: $*
	}
	_EOT
   printf '#
      echo round 1: call
      source ./t11-1.rc
      call r1 call
      echo round 2: xcall
      source ./t11-1.rc
      call r1 xcall
      echo alive and well
   #' | ${MAILX} ${ARGS} > ./t11 2>&1
   #}}}
   check 11 0 ./t11 '2740730424 120'

   t_epilog "${@}"
} # }}}

t_X_errexit() { # {{{
   t_prolog "${@}"

   if have_feat uistrings; then :; else
      t_echoskip '[!UISTRINGS]'
      t_epilog "${@}"
      return
   fi

   ${cat} <<- '__EOT' > ./t.rc
	echo one
	echos nono
	echo two
	__EOT

   </dev/null ${MAILX} ${ARGS} -Snomemdebug \
         -X'echo one' -X' echos nono ' -X'echo two' \
      > ./t1 2>&1
   check 1 0 ./t1 '2700500141 51'

   </dev/null ${MAILX} ${ARGS} -X'source ./t.rc' -Snomemdebug \
      > ./t2 2>&1
   check 2 0 ./t2 '2700500141 51'

   </dev/null MAILRC=./t.rc ${MAILX} ${ARGS} -:u -Snomemdebug \
      > ./t3 2>&1
   check 3 0 ./t3 '2700500141 51'

   ##

   </dev/null ${MAILX} ${ARGS} -Serrexit -Snomemdebug \
         -X'echo one' -X' echos nono ' -X'echo two' \
      > ./t4 2>&1
   check 4 1 ./t4 '4096689457 47'

   </dev/null ${MAILX} ${ARGS} -X'source ./t.rc' -Serrexit -Snomemdebug \
      > ./t5 2>&1
   check 5 1 ./t5 '4096689457 47'

   </dev/null MAILRC=./t.rc ${MAILX} ${ARGS} -:u -Serrexit -Snomemdebug \
      > ./t6 2>&1
   check 6 1 ./t6 '2710539986 162'

   </dev/null MAILRC=./t.rc ${MAILX} ${ARGS} -:u -Sposix -Snomemdebug \
      > ./t7 2>&1
   check 7 1 ./t7 '2710539986 162'

   ## Repeat 4-7 with ignerr set

   ${sed} -e 's/^echos /ignerr echos /' < ./t.rc > ./t2.rc

   </dev/null ${MAILX} ${ARGS} -Serrexit -Snomemdebug \
         -X'echo one' -X'ignerr echos nono ' -X'echo two' \
      > ./t8 2>&1
   check 8 0 ./t8 '2700500141 51'

   </dev/null ${MAILX} ${ARGS} -X'source ./t2.rc' -Serrexit -Snomemdebug \
      > ./t9 2>&1
   check 9 0 ./t9 '2700500141 51'

   </dev/null MAILRC=./t2.rc ${MAILX} ${ARGS} -:u -Serrexit -Snomemdebug \
      > ./t10 2>&1
   check 10 0 ./t10 '2700500141 51'

   </dev/null MAILRC=./t2.rc ${MAILX} ${ARGS} -:u -Sposix -Snomemdebug \
      > ./t11 2>&1
   check 11 0 ./t11 '2700500141 51'

   # Ensure "good-injection" in a deeper indirection does not cause trouble
   # This actually only works with MLE and HISTORY, and TODO needs a pseudo TTY
   # interaction so that we DO initialize our line editor...
   ${cat} <<- '__EOT' > ./t3.rc
	define oha {
	   return 0
	}
	define x {
	  eval set $xarg
	  echoes time
	  return 0
	}
	__EOT

   printf 'source ./t3.rc\ncall x\necho au'  |
      ${MAILX} ${ARGS} -Snomemdebug -Sxarg=errexit > ./t12 2>&1
   check 12 1 ./t12 '2908921993 44'

   printf 'source ./t3.rc\nset on-history-addition=oha\ncall x\necho au' |
      ${MAILX} ${ARGS} -Snomemdebug -Sxarg=errexit > ./t13 2>&1
   check 13 1 ./t13 '2908921993 44'

   printf 'source ./t3.rc\ncall x\necho au' |
      ${MAILX} ${ARGS} -Snomemdebug -Sxarg=nowhere > ./t14 2>&1
   check 14 0 ./t14 '2049365617 47'

   t_epilog "${@}"
} # }}}

t_Y_errexit() { # {{{
   t_prolog "${@}"

   if have_feat uistrings; then :; else
      t_echoskip '[!UISTRINGS]'
      t_epilog "${@}"
      return
   fi

   ${cat} <<- '__EOT' > ./t.rc
	echo one
	echos nono
	echo two
	__EOT

   </dev/null ${MAILX} ${ARGS} -Snomemdebug \
         -Y'echo one' -Y' echos nono ' -Y'echo two' \
      > ./t1 2>&1
   check 1 0 ./t1 '2700500141 51'

   </dev/null ${MAILX} ${ARGS} -Y'source ./t.rc' -Snomemdebug \
      > ./t2 2>&1
   check 2 0 ./t2 '2700500141 51'

   ##

   </dev/null ${MAILX} ${ARGS} -Serrexit -Snomemdebug \
         -Y'echo one' -Y' echos nono ' -Y'echo two' \
      > ./t3 2>&1
   check 3 1 ./t3 '4096689457 47'

   </dev/null ${MAILX} ${ARGS} -Y'source ./t.rc' -Serrexit -Snomemdebug \
      > ./t4 2>&1
   check 4 1 ./t4 '4096689457 47'

   ## Repeat 3-4 with ignerr set

   ${sed} -e 's/^echos /ignerr echos /' < ./t.rc > ./t2.rc

   </dev/null ${MAILX} ${ARGS} -Serrexit -Snomemdebug \
         -Y'echo one' -Y'ignerr echos nono ' -Y'echo two' \
      > ./t5 2>&1
   check 5 0 ./t5 '2700500141 51'

   </dev/null ${MAILX} ${ARGS} -Y'source ./t2.rc' -Serrexit -Snomemdebug \
      > ./t6 2>&1
   check 6 0 ./t6 '2700500141 51'

   t_epilog "${@}"
} # }}}

t_S_freeze() { # {{{
   t_prolog "${@}"

   # Test basic assumption
   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} \
      -X'echo asksub<$asksub> dietcurd<$dietcurd>' \
      -Xx > ./t1 2>&1
   check 1 0 ./t1 '270686329 21'

   #
   ${cat} <<- '__EOT' > "${BODY}"
	echo asksub<$asksub>
	set asksub
	echo asksub<$asksub>
	__EOT

   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u \
      -Snoasksub -Sasksub -Snoasksub \
      -X'echo asksub<$asksub>' -X'set asksub' -X'echo asksub<$asksub>' \
      -Xx > ./t2 2>&1
   check 2 0 ./t2 '3182942628 37'

   ${cat} <<- '__EOT' > "${BODY}"
	echo asksub<$asksub>
	unset asksub
	echo asksub<$asksub>
	__EOT

   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u \
      -Snoasksub -Sasksub \
      -X'echo asksub<$asksub>' -X'unset asksub' -X'echo asksub<$asksub>' \
      -Xx > ./t3 2>&1
   check 3 0 ./t3 '2006554293 39'

   #
   ${cat} <<- '__EOT' > "${BODY}"
	echo dietcurd<$dietcurd>
	set dietcurd=cherry
	echo dietcurd<$dietcurd>
	__EOT

   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u \
      -Sdietcurd=strawberry -Snodietcurd -Sdietcurd=vanilla \
      -X'echo dietcurd<$dietcurd>' -X'unset dietcurd' \
         -X'echo dietcurd<$dietcurd>' \
      -Xx > ./t4 2>&1
   check 4 0 ./t4 '1985768109 65'

   ${cat} <<- '__EOT' > ./t.rc
	echo dietcurd<$dietcurd>
	unset dietcurd
	echo dietcurd<$dietcurd>
	__EOT

   </dev/null MAILRC=./t.rc ${MAILX} ${ARGS} -:u \
      -Sdietcurd=strawberry -Snodietcurd \
      -X'echo dietcurd<$dietcurd>' -X'set dietcurd=vanilla' \
         -X'echo dietcurd<$dietcurd>' \
      -Xx > ./t5 2>&1
   check 5 0 ./t5 '151574279 51'

   ${cat} <<- '__EOT' > ./t.rc
	!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"
	echo _S_MAILX_TEST<$_S_MAILX_TEST>
	!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"
	set _S_MAILX_TEST=cherry
	!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"
	echo _S_MAILX_TEST<$_S_MAILX_TEST>
	!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"
	__EOT

   </dev/null MAILRC=./t.rc ${MAILX} ${ARGS} -:u \
      -S_S_MAILX_TEST=strawberry -Sno_S_MAILX_TEST -S_S_MAILX_TEST=vanilla \
      -X'echo mail<$_S_MAILX_TEST>' -X'unset _S_MAILX_TEST' \
      -X'!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"' \
      -X'echo _S_MAILX_TEST<$_S_MAILX_TEST>' \
      -Xx > ./t6 2>&1
   check 6 0 ./t6 '3512312216 239'

   ${cat} <<- '__EOT' > ./t.rc
	!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"
	echo _S_MAILX_TEST<$_S_MAILX_TEST>
	!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"
	set _S_MAILX_TEST=cherry
	!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"
	echo _S_MAILX_TEST<$_S_MAILX_TEST>
	!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"
	__EOT

   </dev/null MAILRC=./t.rc ${MAILX} ${ARGS} -:u \
      -S_S_MAILX_TEST=strawberry -Sno_S_MAILX_TEST \
      -X'echo _S_MAILX_TEST<$_S_MAILX_TEST>' -X'set _S_MAILX_TEST=vanilla' \
      -X'!echo "shell says _S_MAILX_TEST<$_S_MAILX_TEST>"' \
      -X'echo _S_MAILX_TEST<$_S_MAILX_TEST>' \
      -Xx > ./t7 2>&1
   check 7 0 ./t7 '167059161 213'

   t_epilog "${@}"
} # }}}

t_f_batch_order() { # {{{
   t_prolog "${@}"

   t__gen_msg subject f-batch-order > ./t.mbox

   # This would exit 64 (EX_USAGE) from ? to [fbddb3b3] (FIX: -f: add
   # n_PO_f_FLAG to avoid that command line order matters)
   </dev/null ${MAILX} ${NOBATCH_ARGS} -R -f -# \
      -Y 'echo du;h;echo da;x' ./t.mbox > ./t1 2>&1
   check 1 0 ./t1 '1690247457 86'

   # And this always worked (hopefully)
   </dev/null ${MAILX} ${NOBATCH_ARGS} -R -# -f \
      -Y 'echo du;h;echo da;x' ./t.mbox > ./t2 2>&1
   check 2 0 ./t2 '1690247457 86'

   t_epilog "${@}"
} # }}}

t_input_inject_semicolon_seq() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1
	define mydeepmac {
		;;;echon '(mydeepmac)';;;
	}
	define mymac {
		echon this_is_mymac;;;;;;call mydeepmac;echon ';';
	}
	echon one';';call mymac;echon two";";;;call mymac;echo three$';';
	define mymac {
		echon this_is_mymac;call mydeepmac;;;echon ,TOO'!;';
	}
	echon one';';call mymac;echon two";";call mymac;echo three$';';
	__EOT
   #}}}
   check 1 0 ./t1 '512117110 140'

   t_epilog "${@}"
} # }}}

t_wysh() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' > ./t.rc
commandali e \\echo
#
e abcd
e a'b'c'd'
e a"b"c"d"
e a$'b'c$'d'
e 'abcd'
e "abcd"
e $'abcd'
e a\ b\ c\ d
e a 'b c' d
e a "b c" d
e a $'b c' d
#
e 'a$`"\'
e "a\$\`'\"\\"
e $'a\$`\'\"\\'
e $'a\$`\'"\\'
# DIET=CURD TIED=
e 'a${DIET}b${TIED}c\${DIET}d\${TIED}e' # COMMENT
e "a${DIET}b${TIED}c\${DIET}d\${TIED}e"
e $'a${DIET}b${TIED}c\${DIET}d\${TIED}e'
#
e a$'\101\0101\x41\u0041\u41\U00000041\U41'c
e a$'\u0041\u41\u0C1\U00000041\U41'c
e a$'\377'c
e a$'\0377'c
e a$'\400'c
e a$'\0400'c
e a$'\U1100001'c
#
e a$'b\0c'd
e a$'b\00c'de
e a$'b\000c'df
e a$'b\0000c'dg
e a$'b\x0c'dh
e a$'b\x00c'di
e a$'b\u0'dj
e a$'b\u00'dk
e a$'b\u000'dl
e a$'b\u0000'dm
e a$'b\U0'dn
e a$'b\U00'do
e a$'b\U000'dp
e a$'b\U0000'dq
e a$'b\U00000'dr
e a$'b\U000000'ds
e a$'b\U0000000'dt
e a$'b\U00000000'du
#
e a$'\cI'b
e a$'\011'b
e a$'\x9'b
e a$'\u9'b
e a$'\U9'b
e a$'\c@'b c d
	__EOT
   #}}}

   if [ -z "${UTF8_LOCALE}" ]; then
      t_echoskip 'unicode:[no UTF-8 locale]'
   else
      < ./t.rc DIET=CURD TIED= \
      LC_ALL=${UTF8_LOCALE} ${MAILX} ${ARGS} > ./tunicode 2>>${ERR}
      check unicode 0 ./tunicode '475805847 317'
   fi

   < ./t.rc DIET=CURD TIED= ${MAILX} ${ARGS} > ./tc 2>>${ERR}
   check c 0 ./tc '1473887148 321'

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t3
   set mager='\hey\'
   varshow mager
   set mager="\hey\\"
   varshow mager
   set mager=$'\hey\\'
   varshow mager
	__EOT
   check 3 0 ./t3 '380053216 54'

   # $(())-good {{{
   # env:I=10,J=33
   ${cat} <<- '__EOT' > ./tarith-good.in
# make this work with (ba)sh \
command -v shopt && shopt -s expand_aliases;\
alias p=printf;alias e=echo;alias s=export
s I=10 J=33
e '= BASE'
e "<$(())>"
e "<$((  ))>"
e "<$((1))>"
e "<$((0))>"
e "<$((0x0))>"
e "<$((0X0))>"
e "<$((000))>"
e "<$((000000000000001))>"
e "<$((2#00000000000000000000000000000000000001))>"
e "<$((0X00000000000000000000000000000000000000000001))>"
e "<$((999999999999999999999999999999999999999999999))>"
e "<$(( 	10	 ))>"
e "<$((9191919191919))>"
e "<$((0xD))>"
e "<$((013))>"
e "<$((32#VV))>"
e "<$((36#ZZ))>"
e "<$((36#zz))>"
e "<$((  64#zzZZ  ))>"
e "<$((64#ZZzz))>"
e "<$((I))>"
e "<$((J))>"
e "<$(( I ))>"
e "<$((	 J 	 ))>"
e "<$(( (1) ))>"
e "<$((((1))))>"
e "<$(((((1)))))>"
e "<$(( (J) ))>"
e "<$((((J))))>"
e "<$(((((J)))))>"
e "<$((	( 	( 	( 	J	 )	 )	 )	 ))>"
e '= UNA PLUS/MINUS'
e "<$((+0))>"
e "<$((    +  0 ))>"
e "<$(( +1))>"
e "<$((+ 1 ))>"
e "<$(( + 4221 ))>"
e "<$(( +0x4221 ))>"
e "<$(( + 64#ZZzz ))>"
e "<$(( +64#ZZzz ))>"
e "<$((+ (1) ))>"
e "<$((+((1))))>"
e "<$((+(((1)))))>"
e "<$((-0))>"
e "<$((    -  0 ))>"
e "<$(( -1))>"
e "<$((- 1 ))>"
e "<$(( - 4221 ))>"
e "<$(( -0x4221 ))>"
e "<$(( - 64#ZZzz ))>"
e "<$(( -64#ZZzz ))>"
e "<$((- (1) ))>"
e "<$((-((1))))>"
e "<$((-(((1)))))>"
e "<$((+ -(1) ))>"
e "<$((+(-(-1))))>"
e "<$((+(-(-(-1)))))>"
e '= UNA !'
e "<$((!0))>"
e "<$((! 00000000))>"
e "<$((!1))>"
e "<$((! 0x00001))>"
e "<$((! - 0))>"
e "<$((!-1))>"
e '= UNA ~'
e "<$((~0))>"
e "<$((~ 00000000))>"
e "<$((~1))>"
e "<$((~ 0x00001))>"
e "<$((~ 64#zz))>"
e "<$((~-1))>"
e "<$((~ - 1))>"
e "<$((~-0))>"
e "<$((~ - 0))>"
e "<$((~(-0)))>"
e "<$((~((- 0))))>"
e '= BIN +'
e "<$((0+0))>"
e "<$(( 0 + 0 ))>"
e "<$((0+1))>"
e "<$(( 0 + 1 ))>"
e "<$((1+0))>"
e "<$(( 1 + 0 ))>"
e "<$((1+1))>"
e "<$(( 1 + 1 ))>"
e "<$(( (1 + 1) ))>"
e "<$(((((((-1)))) + (((-1))))))>"
e "<$((1111+2222))>"
e "<$((2222+1111))>"
e "<$(( +0x10 + +0x11 ))>"
e "<$(( -0x10 + -0x11 ))>"
e "<$(( -0x10 + -0x11 ))>"
e "<$(( +64#10 + -64#11 ))>"
e "<$(( +0x11 + +0x10 ))>"
e "<$(( -0x11 + -0x10 ))>"
e "<$(( -0x11 + -0x10 ))>"
e "<$(( +64#11 + -64#10 ))>"
e "<$((0x8000000000000000+-1))>"
e "<$((0x8000000000000000+1))>"
e "<$((0x7FFFFFFFFFFFFFFF+-1))>"
e "<$((0x7FFFFFFFFFFFFFFF+1))>"
e "<$((0xFFFFFFFFFFFFFFFF+-1))>"
e "<$((0xFFFFFFFFFFFFFFFF+1))>"
e "<$((0x8000000000000000+-11))>"
e "<$((0x8000000000000000+11))>"
e "<$((0x7FFFFFFFFFFFFFFF+-11))>"
e "<$((0x7FFFFFFFFFFFFFFF+11))>"
e "<$((0xFFFFFFFFFFFFFFFF+-11))>"
e "<$((0xFFFFFFFFFFFFFFFF+11))>"
e '= BIN -'
e "<$((0-0))>"
e "<$(( 0 - 0 ))>"
e "<$((0-1))>"
e "<$(( 0 - 1 ))>"
e "<$((1-0))>"
e "<$(( 1 - 0 ))>"
e "<$((1-1))>"
e "<$(( 1 - 1 ))>"
e "<$(( (1 - 1) ))>"
e "<$(((((((+1)))) - (((+1))))))>"
e "<$((1111-2222))>"
e "<$((2222-1111))>"
e "<$(( +0x10 - +0x11 ))>"
e "<$(( -0x10 - -0x11 ))>"
e "<$(( -0x10 - -0x11 ))>"
e "<$(( +64#10 - -64#11 ))>"
e "<$(( +0x11 - +0x10 ))>"
e "<$(( -0x11 - -0x10 ))>"
e "<$(( -0x11 - -0x10 ))>"
e "<$(( +64#11 - -64#10 ))>"
e "<$((0x8000000000000000--1))>"
e "<$((0x8000000000000000-1))>"
e "<$((0x7FFFFFFFFFFFFFFF--1))>"
e "<$((0x7FFFFFFFFFFFFFFF-1))>"
e "<$((0xFFFFFFFFFFFFFFFF--1))>"
e "<$((0xFFFFFFFFFFFFFFFF-1))>"
e "<$((0x8000000000000000--11))>"
e "<$((0x8000000000000000-11))>"
e "<$((0x7FFFFFFFFFFFFFFF--11))>"
e "<$((0x7FFFFFFFFFFFFFFF-11))>"
e "<$((0xFFFFFFFFFFFFFFFF--11))>"
e "<$((0xFFFFFFFFFFFFFFFF-11))>"
e '= BIN *'
e "<$((0*0))>"
e "<$(( 0 * 0 ))>"
e "<$((0*1))>"
e "<$(( 0 * 1 ))>"
e "<$((1*0))>"
e "<$(( 1 * 0 ))>"
e "<$((1*1))>"
e "<$(( 1 * 1 ))>"
e "<$((1111*2222))>"
e "<$((2222*1111))>"
e "<$(( +0x10 * +0x11 ))>"
e "<$(( -0x10 * -0x11 ))>"
e "<$(( -0x10 * -0x11 ))>"
e "<$(( +64#10 * -64#11 ))>"
e "<$(( +0x11 * +0x10 ))>"
e "<$(( -0x11 * -0x10 ))>"
e "<$(( -0x11 * -0x10 ))>"
e "<$(( +64#11 * -64#10 ))>"
e "<$((0x8000000000000000*-1))>"
e "<$((0x8000000000000000*1))>"
e "<$((0x7FFFFFFFFFFFFFFF*-1))>"
e "<$((0x7FFFFFFFFFFFFFFF*1))>"
e "<$((0xFFFFFFFFFFFFFFFF*-1))>"
e "<$((0xFFFFFFFFFFFFFFFF*1))>"
e "<$((0x8000000000000000*-11))>"
e "<$((0x8000000000000000*11))>"
e "<$((0x7FFFFFFFFFFFFFFF*-11))>"
e "<$((0x7FFFFFFFFFFFFFFF*11))>"
e "<$((0xFFFFFFFFFFFFFFFF*-11))>"
e "<$((0xFFFFFFFFFFFFFFFF*11))>"
e '= BIN /'
e "<$(( 0 / 1 ))>"
e "<$((1/1))>"
e "<$(( 1 / 1 ))>"
e "<$((1111/2222))>"
e "<$((2222/1111))>"
e "<$(( +0x10 / +0x11 ))>"
e "<$(( -0x10 / -0x11 ))>"
e "<$(( -0x10 / -0x11 ))>"
e "<$(( +64#10 / -64#11 ))>"
e "<$(( +0x11 / +0x10 ))>"
e "<$(( -0x11 / -0x10 ))>"
e "<$(( -0x11 / -0x10 ))>"
e "<$(( +64#11 / -64#10 ))>"
e "<$((2/1))>"
e "<$((3/1))>"
e "<$((3/2))>"
e "<$((3/3))>"
e "<$((3/4))>"
e "<$((-1/4))>"
e "<$((0x8000000000000000/-1))>"
e "<$((0x8000000000000000/1))>"
e "<$((0x7FFFFFFFFFFFFFFF/-1))>"
e "<$((0x7FFFFFFFFFFFFFFF/1))>"
e "<$((0xFFFFFFFFFFFFFFFF/-1))>"
e "<$((0xFFFFFFFFFFFFFFFF/1))>"
e "<$((0x8000000000000000/-11))>"
e "<$((0x8000000000000000/11))>"
e "<$((0x7FFFFFFFFFFFFFFF/-11))>"
e "<$((0x7FFFFFFFFFFFFFFF/11))>"
e "<$((0xFFFFFFFFFFFFFFFF/-11))>"
e "<$((0xFFFFFFFFFFFFFFFF/11))>"
e '= BIN %'
e "<$(( 0 % 1 ))>"
e "<$((1%1))>"
e "<$(( 1 % 1 ))>"
e "<$((1111%2222))>"
e "<$((2222%1111))>"
e "<$(( +0x10 % +0x11 ))>"
e "<$(( -0x10 % -0x11 ))>"
e "<$(( -0x10 % -0x11 ))>"
e "<$(( +64#10 % -64#11 ))>"
e "<$(( +0x11 % +0x10 ))>"
e "<$(( -0x11 % -0x10 ))>"
e "<$(( -0x11 % -0x10 ))>"
e "<$(( +64#11 % -64#10 ))>"
e "<$((2%1))>"
e "<$((3%1))>"
e "<$((3%2))>"
e "<$((3%3))>"
e "<$((3%4))>"
e "<$((-1%4))>"
e "<$((0x8000000000000000%-1))>"
e "<$((0x8000000000000000%1))>"
e "<$((0x7FFFFFFFFFFFFFFF%-1))>"
e "<$((0x7FFFFFFFFFFFFFFF%1))>"
e "<$((0xFFFFFFFFFFFFFFFF%-1))>"
e "<$((0xFFFFFFFFFFFFFFFF%1))>"
e "<$((0x8000000000000000%-11))>"
e "<$((0x8000000000000000%11))>"
e "<$((0x7FFFFFFFFFFFFFFF%-11))>"
e "<$((0x7FFFFFFFFFFFFFFF%11))>"
e "<$((0xFFFFFFFFFFFFFFFF%-11))>"
e "<$((0xFFFFFFFFFFFFFFFF%11))>"
e '= BIN <<'
e "<$((0<<0))>"
e "<$(( 0 << 0 ))>"
e "<$((0<<1))>"
e "<$(( 0 << 1 ))>"
e "<$((1<<0))>"
e "<$(( 1 << 0 ))>"
e "<$((1<<1))>"
e "<$(( 1 << 1 ))>"
e "<$((1111<<2222))>"
e "<$((2222<<1111))>"
e "<$(( +0x10 << +0x11 ))>"
e "<$(( -0x10 << -0x11 ))>"
e "<$(( -0x10 << -0x11 ))>"
e "<$(( +64#10 << -64#11 ))>"
e "<$(( +0x11 << +0x10 ))>"
e "<$(( -0x11 << -0x10 ))>"
e "<$(( -0x11 << -0x10 ))>"
e "<$(( +64#11 << -64#10 ))>"
e "<$(( +64 << +1024 ))>"
e "<$((0x8000000000000000<<-1))>"
e "<$((0x8000000000000000<<1))>"
e "<$((0x7FFFFFFFFFFFFFFF<<-1))>"
e "<$((0x7FFFFFFFFFFFFFFF<<1))>"
e "<$((0xFFFFFFFFFFFFFFFF<<-1))>"
e "<$((0xFFFFFFFFFFFFFFFF<<1))>"
e "<$((0x8000000000000000<<-11))>"
e "<$((0x8000000000000000<<11))>"
e "<$((0x7FFFFFFFFFFFFFFF<<-11))>"
e "<$((0x7FFFFFFFFFFFFFFF<<11))>"
e "<$((0xFFFFFFFFFFFFFFFF<<-11))>"
e "<$((0xFFFFFFFFFFFFFFFF<<11))>"
e '= BIN >>'
e "<$((0>>0))>"
e "<$(( 0 >> 0 ))>"
e "<$((0>>1))>"
e "<$(( 0 >> 1 ))>"
e "<$((1>>0))>"
e "<$(( 1 >> 0 ))>"
e "<$((1>>1))>"
e "<$(( 1 >> 1 ))>"
e "<$((1>>>1))>"
e "<$(( 1 >>> 1 ))>"
e "<$((1111>>2222))>"
e "<$((2222>>1111))>"
e "<$((1111>>>2222))>"
e "<$((2222>>>1111))>"
e "<$(( +0x10 >> +0x11 ))>"
e "<$(( -0x10 >> -0x11 ))>"
e "<$(( -0x10 >> -0x11 ))>"
e "<$(( -0x10 >>> -0x11 ))>"
e "<$(( +64#10 >> -64#11 ))>"
e "<$(( +0x11 >> +0x10 ))>"
e "<$(( -0x11 >> -0x10 ))>"
e "<$(( -0x11 >> -0x10 ))>"
e "<$(( +64#11 >> -64#10 ))>"
e "<$(( +64 >> +1024 ))>"
e "<$((0x8000000000000000>>-1))>"
e "<$((0x8000000000000000>>1))>"
e "<$((0x7FFFFFFFFFFFFFFF>>-1))>"
e "<$((0x7FFFFFFFFFFFFFFF>>1))>"
e "<$((0xFFFFFFFFFFFFFFFF>>-1))>"
e "<$((0xFFFFFFFFFFFFFFFF>>1))>"
e "<$((0x8000000000000000>>-11))>"
e "<$((0x8000000000000000>>11))>"
e "<$((0x7FFFFFFFFFFFFFFF>>-11))>"
e "<$((0x7FFFFFFFFFFFFFFF>>11))>"
e "<$((0xFFFFFFFFFFFFFFFF>>-11))>"
e "<$((0xFFFFFFFFFFFFFFFF>>11))>"
e "<$((0xFFFFFFFFFFFFFFFF>>>11))>"
e '= BIN **'
e "<$((0**1))>"
e "<$((2**1))>"
e "<$((2**2))>"
e "<$((2**3))>"
e "<$((2**4))>"
e "<$((10**4))>"
e "<$((10**10))>"
e "<$((10**5+5))>"
e "<$((10**(5+5)))>"
e '= LOG OR'
e "<$((0||0))>"
e "<$(( 000  ||  0X0  ))>"
e "<$((01 || 64#1))>"
e "<$((01 || 64#1))>"
e "<$((0x1234 || 4660))>"
e "<$((0x1234 || 011064))>"
s I=33 J=33;e "<$((I||J))>"
s I=33 J=33;e "<$((  I 	  || 	 J   ))>"
e "<$((0||1))>"
e "<$((0||0000000000000000000000001))>"
e "<$((1||2))>"
e "<$((0x1234 || 04660))>"
e "<$((0x1234 || 0x11064))>"
s I=10 J=33;e "<$((I||J))>"
s I=-10 J=-33;e "<$((I||J))>"
s I=-33 J=-33;e "<$((I||J))>"
s I=0 J=-33;e "<$((I||J))>"
s I=33 J=0;e "<$((I||J))>"
e '= LOG AND'
e "<$((0&&0))>"
e "<$(( 000  &&  0X0  ))>"
e "<$((01 && 64#1))>"
e "<$((01 && 64#1))>"
e "<$((0x1234 && 4660))>"
e "<$((0x1234 && 011064))>"
s I=33 J=33;e "<$((I&&J))>"
s I=33 J=33;e "<$((  I 	  && 	 J   ))>"
e "<$((0&&1))>"
e "<$((0&&0000000000000000000000001))>"
e "<$((1&&2))>"
e "<$((0x1234 && 04660))>"
e "<$((0x1234 && 0x11064))>"
s I=10 J=33;e "<$((I&&J))>"
s I=-10 J=-33;e "<$((I&&J))>"
s I=-33 J=-33;e "<$((I&&J))>"
s I=0 J=-33;e "<$((I&&J))>"
s I=33 J=0;e "<$((I&&J))>"
e '= BIN BIT_OR'
e "<$((0|0))>"
e "<$(( 0 | 0 ))>"
e "<$((0|1))>"
e "<$(( 0 | 1 ))>"
e "<$((1|0))>"
e "<$(( 1 | 0 ))>"
e "<$((1|1))>"
e "<$(( 1 | 1 ))>"
e "<$((1111|2222))>"
e "<$((2222|1111))>"
e "<$(( +0x10 | +0x11 ))>"
e "<$(( -0x10 | -0x11 ))>"
e "<$(( -0x10 | -0x11 ))>"
e "<$(( +64#10 | -64#11 ))>"
e "<$(( +0x11 | +0x10 ))>"
e "<$(( -0x11 | -0x10 ))>"
e "<$(( -0x11 | -0x10 ))>"
e "<$(( +64#11 | -64#10 ))>"
e "<$(( +64 | +1024 ))>"
e "<$((0x8000000000000000|-1))>"
e "<$((0x8000000000000000|1))>"
e "<$((0x7FFFFFFFFFFFFFFF|-1))>"
e "<$((0x7FFFFFFFFFFFFFFF|1))>"
e "<$((0xFFFFFFFFFFFFFFFF|-1))>"
e "<$((0xFFFFFFFFFFFFFFFF|1))>"
e "<$((0x8000000000000000|-11))>"
e "<$((0x8000000000000000|11))>"
e "<$((0x7FFFFFFFFFFFFFFF|-11))>"
e "<$((0x7FFFFFFFFFFFFFFF|11))>"
e "<$((0xFFFFFFFFFFFFFFFF|-11))>"
e "<$((0xFFFFFFFFFFFFFFFF|11))>"
e '= BIN BIT_XOR'
e "<$((0^0))>"
e "<$(( 0 ^ 0 ))>"
e "<$((0^1))>"
e "<$(( 0 ^ 1 ))>"
e "<$((1^0))>"
e "<$(( 1 ^ 0 ))>"
e "<$((1^1))>"
e "<$(( 1 ^ 1 ))>"
e "<$((1111^2222))>"
e "<$((2222^1111))>"
e "<$(( +0x10 ^ +0x11 ))>"
e "<$(( -0x10 ^ -0x11 ))>"
e "<$(( -0x10 ^ -0x11 ))>"
e "<$(( +64#10 ^ -64#11 ))>"
e "<$(( +0x11 ^ +0x10 ))>"
e "<$(( -0x11 ^ -0x10 ))>"
e "<$(( -0x11 ^ -0x10 ))>"
e "<$(( +64#11 ^ -64#10 ))>"
e "<$(( +64 ^ +1024 ))>"
e "<$((0x8000000000000000^-1))>"
e "<$((0x8000000000000000^1))>"
e "<$((0x7FFFFFFFFFFFFFFF^-1))>"
e "<$((0x7FFFFFFFFFFFFFFF^1))>"
e "<$((0xFFFFFFFFFFFFFFFF^-1))>"
e "<$((0xFFFFFFFFFFFFFFFF^1))>"
e "<$((0x8000000000000000^-11))>"
e "<$((0x8000000000000000^11))>"
e "<$((0x7FFFFFFFFFFFFFFF^-11))>"
e "<$((0x7FFFFFFFFFFFFFFF^11))>"
e "<$((0xFFFFFFFFFFFFFFFF^-11))>"
e "<$((0xFFFFFFFFFFFFFFFF^11))>"
e '= BIN BIT_AND'
e "<$((0&0))>"
e "<$(( 0 & 0 ))>"
e "<$((0&1))>"
e "<$(( 0 & 1 ))>"
e "<$((1&0))>"
e "<$(( 1 & 0 ))>"
e "<$((1&1))>"
e "<$(( 1 & 1 ))>"
e "<$((1111&2222))>"
e "<$((2222&1111))>"
e "<$(( +0x10 & +0x11 ))>"
e "<$(( -0x10 & -0x11 ))>"
e "<$(( -0x10 & -0x11 ))>"
e "<$(( +64#10 & -64#11 ))>"
e "<$(( +0x11 & +0x10 ))>"
e "<$(( -0x11 & -0x10 ))>"
e "<$(( -0x11 & -0x10 ))>"
e "<$(( +64#11 & -64#10 ))>"
e "<$(( +64 & +1024 ))>"
e "<$((0x8000000000000000&-1))>"
e "<$((0x8000000000000000&1))>"
e "<$((0x7FFFFFFFFFFFFFFF&-1))>"
e "<$((0x7FFFFFFFFFFFFFFF&1))>"
e "<$((0xFFFFFFFFFFFFFFFF&-1))>"
e "<$((0xFFFFFFFFFFFFFFFF&1))>"
e "<$((0x8000000000000000&-11))>"
e "<$((0x8000000000000000&11))>"
e "<$((0x7FFFFFFFFFFFFFFF&-11))>"
e "<$((0x7FFFFFFFFFFFFFFF&11))>"
e "<$((0xFFFFFFFFFFFFFFFF&-11))>"
e "<$((0xFFFFFFFFFFFFFFFF&11))>"
e '= BIN EQ'
e "<$((0==0))>"
e "<$(( 000  ==  0X0  ))>"
e "<$((01 == 64#1))>"
e "<$((01 == 64#1))>"
e "<$((0x1234 == 4660))>"
e "<$((0x1234 == 011064))>"
s I=33 J=33;e "<$((I==J))>"
s I=33 J=33;e "<$((  I 	  == 	 J   ))>"
e "<$((0==1))>"
e "<$((0==0000000000000000000000001))>"
e "<$((1==2))>"
e "<$((0x1234 == 04660))>"
e "<$((0x1234 == 0x11064))>"
s I=10 J=33;e "<$((I==J))>"
s I=-10 J=-33;e "<$((I==J))>"
s I=-33 J=-33;e "<$((I==J))>"
e '= BIN NE'
e "<$((0!=0))>"
e "<$(( 000  !=  0X0  ))>"
e "<$((01 != 64#1))>"
e "<$((01 != 64#1))>"
e "<$((0x1234 != 4660))>"
e "<$((0x1234 != 011064))>"
s I=33 J=33;e "<$((I!=J))>"
s I=33 J=33;e "<$((  I 	  != 	 J   ))>"
e "<$((0!=1))>"
e "<$((0!=0000000000000000000000001))>"
e "<$((1!=2))>"
e "<$((0x1234 != 04660))>"
e "<$((0x1234 != 0x11064))>"
s I=10 J=33;e "<$((I!=J))>"
s I=-10 J=-33;e "<$((I!=J))>"
s I=-33 J=-33;e "<$((I!=J))>"
e '= BIN LE'
e "<$((0<=0))>"
e "<$(( 000  <=  0X0  ))>"
e "<$((01 <= 64#1))>"
e "<$((01 <= 64#2))>"
e "<$((02 <= 64#1))>"
e "<$((0x1234 <= 4660))>"
e "<$((0x1234 <= 011064))>"
e "<$((0x1233 <= 011064))>"
e "<$((0x1235 <= 011064))>"
s I=33 J=33;e "<$((I<=J))>"
s I=33 J=33;e "<$((I<=J))>"
s I=32 J=33;e "<$((I<=J))>"
s I=34 J=33;e "<$((I<=J))>"
s I=-33 J=-33;e "<$((I<=J))>"
s I=-33 J=-33;e "<$((I<=J))>"
s I=-32 J=-33;e "<$((I<=J))>"
s I=-34 J=-33;e "<$((I<=J))>"
e '= BIN GE'
e "<$((0>=0))>"
e "<$(( 000  >=  0X0  ))>"
e "<$((01 >= 64#1))>"
e "<$((01 >= 64#2))>"
e "<$((02 >= 64#1))>"
e "<$((0x1234 >= 4660))>"
e "<$((0x1234 >= 011064))>"
e "<$((0x1233 >= 011064))>"
e "<$((0x1235 >= 011064))>"
s I=33 J=33;e "<$((I>=J))>"
s I=33 J=33;e "<$((I>=J))>"
s I=32 J=33;e "<$((I>=J))>"
s I=34 J=33;e "<$((I>=J))>"
s I=-33 J=-33;e "<$((I>=J))>"
s I=-33 J=-33;e "<$((I>=J))>"
s I=-32 J=-33;e "<$((I>=J))>"
s I=-34 J=-33;e "<$((I>=J))>"
e '= BIN LT'
e "<$((0<0))>"
e "<$(( 000  <  0X0  ))>"
e "<$((01 < 64#1))>"
e "<$((01 < 64#2))>"
e "<$((02 < 64#1))>"
e "<$((0x1234 < 4660))>"
e "<$((0x1234 < 011064))>"
e "<$((0x1233 < 011064))>"
e "<$((0x1235 < 011064))>"
s I=33 J=33;e "<$((I<J))>"
s I=33 J=33;e "<$((I<J))>"
s I=32 J=33;e "<$((I<J))>"
s I=34 J=33;e "<$((I<J))>"
s I=-33 J=-33;e "<$((I<J))>"
s I=-33 J=-33;e "<$((I<J))>"
s I=-32 J=-33;e "<$((I<J))>"
s I=-34 J=-33;e "<$((I<J))>"
e '= BIN GT'
e "<$((0>0))>"
e "<$(( 000  >  0X0  ))>"
e "<$((01 > 64#1))>"
e "<$((01 > 64#2))>"
e "<$((02 > 64#1))>"
e "<$((0x1234 > 4660))>"
e "<$((0x1234 > 011064))>"
e "<$((0x1233 > 011064))>"
e "<$((0x1235 > 011064))>"
s I=33 J=33;e "<$((I>J))>"
s I=33 J=33;e "<$((I>J))>"
s I=32 J=33;e "<$((I>J))>"
s I=34 J=33;e "<$((I>J))>"
s I=-33 J=-33;e "<$((I>J))>"
s I=-33 J=-33;e "<$((I>J))>"
s I=-32 J=-33;e "<$((I>J))>"
s I=-34 J=-33;e "<$((I>J))>"
#
# COMMA below
e '= PRECEDENCE I'
e "<$(( 1 + 2 + 3 ))>"
e "<$(( 1 - 2 + 3 ))>"
e "<$(( 3 - 2 - 1 ))>"
e "<$(( 3 - 2 + 1 ))>"
e "<$(( - 2 + 1 ))>"
e "<$(( 2 + -1 ))>"
e "<$(( ! 2 + 1 ))>"
e "<$(( 2 + !1 ))>"
e "<$(( 3 * 2 + 2 ))>"
e "<$(( 3 + 2 * 2 ))>"
e "<$(( 3 * 2 * 2 ))>"
e "<$(( 9 / 3 + 2 ))>"
e "<$(( 9 + 3 / 2 ))>"
e "<$(( 9 / 3 / 2 ))>"
e "<$(( 9 << 1 + 2 ))>"
e "<$(( 9 + 3 << 2 ))>"
e "<$(( 9 << 3 << 2 ))>"
e "<$(( 9 >> 1 + 2 ))>"
e "<$(( 9 + 3 >> 2 ))>"
e "<$(( 19 >> 3 >> 1 ))>"
e "<$(( 19 >> 3 << 1 ))>"
e "<$(( 19 << 3 >> 1 ))>"
e "<$(( 2 + 3 < 3 * 2 ))>"
e "<$(( 2 << 3 >= 3 << 2 ))>"
e "<$(( 0xfD & 0xF == 0xF ))>"
e "<$((0xfD&0xF==0xF))>"
e "<$(( 3 * 7 , 2 << 8 ,  9 - 7 ))>"
e "<$((3*7,2<<8,9-7))>"
e '= PARENS'
e "<$(((1 + 2) + 3))>"
e "<$(((1+2)+3))>"
e "<$((1 - (2 + 3)))>"
e "<$((1-(2+3)))>"
e "<$((3 - (2 - 1)))>"
e "<$((3-(2-1)))>"
e "<$((3 - ( 2 + 1 )))>"
e "<$((3-(2+1)))>"
e "<$((- (2 + 1)))>"
e "<$((-(2+1)))>"
e "<$((! (2 + 1)))>"
e "<$((!(2+1)))>"
e "<$((3 * (2 + 2)))>"
e "<$((3*(2+2)))>"
e "<$(((3 + 2) * 2))>"
e "<$(((3+2)*2))>"
e "<$((3 * (2 * 2)))>"
e "<$((3*(2*8)))>"
e "<$((9 / (3 + 2)))>"
e "<$((9/(3+2)))>"
e "<$((( 9 + 3 ) / 2))>"
e "<$(((9+3)/2))>"
e "<$((9 / ( 3 / 2 )))>"
e "<$((9/(3/2)))>"
e "<$((( 9 << 1 ) + 2))>"
e "<$(((9<<1)+2))>"
e "<$((9 + (3 << 2)))>"
e "<$((9+(3<<2)))>"
e "<$((9 << (3 << 2)))>"
e "<$((9<<(3<<2)))>"
e "<$(((9 >> 1) + 2))>"
e "<$(((9>>1)+2))>"
e "<$((9 + (3 >> 2)))>"
e "<$((9+(3>>2)))>"
e "<$((19 >> (3 >> 1)))>"
e "<$((19>>(3>>1)))>"
e "<$((19 >> (3 << 1)))>"
e "<$((19>>(3<<1)))>"
e "<$((19 << (3 >> 1)))>"
e "<$((19<<(3>>1)))>"
e "<$((2 + (3 < 3) * 2))>"
e "<$((2+(3<3)*2))>"
e "<$((2 << ((3 >= 3) << 2)))>"
e "<$((2<<((3>=3)<<2)))>"
e "<$(((0xfD & 0xF) == 0xF))>"
e "<$(((0xfD&0xF)==0xF))>"
e "<$((3 * (7 , 2) << (8 ,  9 - 7)))>"
e "<$((3*(7,2)<<(8,9-7)))>"
#
# COND BELOW
e '= ASSIGN I'
unset I;p "<$(( I = 3 ))>";e "<$I>"
unset I;p "<$((I=3))>";e "<$I>"
s I=10;p "<$((I=3))>";e "<$I>"
s I=10;p "<$((I+=1))>";e "<$I>"
s I=10;p "<$((I-=1))>";e "<$I>"
s I=10;p "<$((I*=1))>";e "<$I>"
s I=10;p "<$((I*=2))>";e "<$I>"
s I=10;p "<$((I/=1))>";e "<$I>"
s I=10;p "<$((I/=2))>";e "<$I>"
s I=10;p "<$((I%=1))>";e "<$I>"
s I=10;p "<$((I%=2))>";e "<$I>"
s I=10;p "<$((I**=1))>";e "<$I>"
s I=10;p "<$((I**=2))>";e "<$I>"
s I=10;p "<$((I**=1+1))>";e "<$I>"
s I=10;p "<$((I|=1))>";e "<$I>"
s I=10;p "<$((I^=1))>";e "<$I>";p "<$((I^=1))>";e "<$I>"
s I=10;p "<$((I&=2))>";e "<$I>"
s I=10;p "<$((I>>=1))>";e "<$I>"
s I=10;p "<$((I<<=1))>";e "<$I>"
s I=-1;p "<$((I>>>=1))>";e "<$I>"
e '= ASSIGN II'
s I=2;p "<$(((I+=1)-1))>";e "<$I>"
s I=4;p "<$(((I-=1)+1))>";e "<$I>"
s I=0 J=0;p "<$(((I=5)*(J=7)+1))>";e "<$I><$J>"
s I=99 J=17;p "<$(((I+=1)*(J-=2)+1))>";e "<$I><$J>"
s I=10;p "<$((I=2,I|=1))>";e "<$I>"
s I=0 J=0 Y=0 Z=0;p "<$((I=1,J=2,Y=3,Z=4,Z+=I+J+Y))>";e "<$I><$J><$Y><$Z>"
e '= POSTFIX'
s I=1;p "<$((I++))>";e "<$I>"
s I=1 J=0;p "<$((J=I++))>";e "<$I><$J>"
s I=1 J=10;p "<$((J++*I++))>";e "<$I><$J>"
s I=1 J=10;p "<$(((J++)*(I++)))>";e "<$I><$J>"
s I=1;p "<$((I--))>";e "<$I>"
s I=1 J=0;p "<$((J=I--))>";e "<$I><$J>"
s I=1 J=10;p "<$((J--*I--))>";e "<$I><$J>"
s I=1 J=10;p "<$(((J--)*(I--)))>";e "<$I><$J>"
e '= PREFIX'
s I=1;p "<$((++I))>";e "<$I>"
s I=1 J=0;p "<$((J=++I))>";e "<$I><$J>"
s I=1 J=10;p "<$((++J*++I))>";e "<$I><$J>"
s I=1 J=10;p "<$((++(J)*++(I)))>";e "<$I><$J>"
s I=1 J=10;p "<$(((++J)*(++I)))>";e "<$I><$J>"
s I=1;p "<$((--I))>";e "<$I>"
s I=1 J=0;p "<$((J=--I))>";e "<$I><$J>"
s I=2 J=10;p "<$((--J*--I))>";e "<$I><$J>"
s I=1 J=10;p "<$((--(J)*--(I)))>";e "<$I><$J>"
s I=1 J=10;p "<$(((--J)*(--I)))>";e "<$I><$J>"
e '= VAR RECUR'
s I='1 + 1';p "<$((I))>";e "<$I>"
s I='1 + 1';p "<$((+I))>";e "<$I>"
s I='1 + 1';p "<$((++I))>";e "<$I>"
s I='1 + 1';p "<$((I++))>";e "<$I>"
s I='1 + 1';p "<$((1+I))>";e "<$I>"
s I='1 + 1 * 2';p "<$((I+1))>";e "<$I>"
s I='(1 + 1) * 2';p "<$((I+1))>";e "<$I>"
s I='1 + 1' J='3 / 2';p "<$((I=I+J))>";e "<$I><$J>"
s I='1 + 1';p "<$((I=I))>";e "<$I>"
s I='1 + 1';p "<$((I=+I))>";e "<$I>"
s I='1 + 1';p "<$((I=1+I))>";e "<$I>"
s I='1 + 1 * 2';p "<$((I=I+1))>";e "<$I>"
s I='(1 + 1) * 2';p "<$((I=I+1))>";e "<$I>"
s I='1 + 1' J='3 / 2';p "<$((I+=I+J))>";e "<$I><$J>"
e '= COMMA'
e "<$(( 1 , 2 ))>"
e "<$(( 1 , 2 , 3 ))>"
e "<$(( 1 , 2	,	3 , 4 ))>"
e "<$((1,2,3,4))>"
s I='1 + 1';p "<$(( I=10 , I+=I, I=I**2, I/=3 ))>";e "<$I>"
s I1=I2=10 I2=3;p "<$((I1,I2))>";e "<$I1><$I2>"
e '= COND'
e "<$(( +0 ? 2 : 3 ))>"
e "<$((-0?2:3))>"
e "<$(( +1 ? 2 : 3 ))>"
e "<$(( 1-1 ? 2 : 3 ))>"
e "<$(( 1-0 ? 2 : 3 ))>"
e "<$((-1?2:3))>"
e "<$(( 0x1234 ? 111 : 222 ))>"
e "<$((1**2 ? 5 : 7))>"
e "<$((0**2 ? 5 : 7))>"
e "<$((0**2>=0?5:7))>"
e "<$((-1<=0**2?5:7))>"
e "<$((1<=0**2?5:7))>"
e "<$((1>2||1*0?5:7))>"
e "<$((1>2&&1*0?5:7))>"
e "<$((1<2&&1*0?5:7))>"
e "<$((1<2&&1*0+1?5:7))>"
e '-- COND .2'
e "<$(( 1 < 2 ? -1 : 1 > 2 ? 1 : 0 ))>"
e "<$((1 < 1 ? -1 : 1 > 1 ? 1 : 0))>"
e "<$((2<1?-1:2>1?1:0))>"
e "<$((4<5 ? 1 : 32))>"
e "<$((4>5 ? 1 : 32))>"
e "<$((4>(2+3) ? 1 : 32))>"
e "<$((4<(2+3) ? 1 : 32))>"
e "<$(((2+2)<(2+3) ? 1 : 32))>"
e "<$(((2+2)>(2+3) ? 1 : 32))>"
## grouping protects precedence in : parts (syntax error tests below)
e '-- COND .3'
e "<$((1-1 < 1 ? 2,4 : 1,3))>"
e "<$((0<1?2,4:(1,3)))>"
e "<$((0,1,2,0?2,4:1,3))>"
e "<$((0,1,2,1?2,4:1,3))>"
e "<$((0,1,2,0?2,4:(1,3)))>"
e "<$((0,1,2,1?2,4:(1,3)))>"
e "<$((0,1,2,0?(2,4):1,3))>"
e "<$((0,1,2,1?(2,4):1,3))>"
e "<$((0,1,2,0?(2,4):(1,3)))>"
e "<$((0,1,2,1?(2,4):(1,3)))>"
e "<$((0?2:((0,3)?1:4)))>"
e "<$((1?2:3,0?1:4))>"
e "<$((1?2:3,0?1:4?5:6))>"
e "<$((1?2:(3,0)?1:4?5:6))>"
e "<$((1?2:3,0?4,5:5,6?7,8:9,10))>"
e "<$((1?2:(3,0)?4,5:5,6?7,8:9,10))>"
e "<$((1?2:(3,0)?(4,5):5,6?7,8:9,10))>"
e "<$((1?2:(3,0)?(4,5):(5,6)?7,8:9,10))>"
e "<$((1?2:(3,0)?(4,5):(5,6)?(7,8):9,10))>"
e "<$((1?2:(3,0)?(4,5):(5,6)?(7,8):(9,10)))>"
e "<$((1?2:3,1?4,5:5,6?7,8:9,10))>"
e "<$((1?2:(3,1)?4,5:5,6?7,8:9,10))>"
e "<$((1?2:(3,1)?(4,5):5,6?7,8:9,10))>"
e "<$((1?2:(3,1)?(4,5):(5,6)?7,8:9,10))>"
e "<$((1?2:(3,1)?(4,5):(5,6)?(7,8):9,10))>"
e "<$((1?2:(3,1)?(4,5):(5,6)?(7,8):(9,10)))>"
e "<$((0?2:(3,1)?(4,5):(5,6)?(7,8):(9,10)))>"
e "<$((0?2:(3,1)?4,5:(5,6)?7,8:(9,10)))>"
e "<$((0?2:(3,0)?(4,5):(5,6)?(7,8):(9,10)))>"
e "<$((0?2:(3,0)?4,5:(5,6)?7,8:(9,10)))>"
e "<$((0?2:(3,0)?(4,5):(5,0)?(7,8):(9,10)))>"
e "<$((0?2:(3,0)?4,5:(5,0)?7,8:(9,10)))>"
e "<$((0?2:3,0?4,5:(5,0)?7,8:(9,10)))>"
e "<$((0?2:(3,0)?4,5:5,0?7,8:(9,10)))>"
e "<$((0?2:(3,0)?4,5:(5,0)?7,8:9,10))>"
e '-- COND .4'
e "<$((1?2?3?4?5:6:7:8:9))>"
e "<$((1?2?3?0?5:6:7:8:9))>"
e "<$((1?2?0?0?5:6:7:8:9))>"
e "<$((1?0?0?0?5:6:7:8:9))>"
e "<$((0?0?0?0?5:6:7:8:9))>"
e "<$((0?3+4?10:11:5+6?12:13))>"
e "<$((1?3+4?10:11:5+6?12:13))>"
e "<$((0?(3+4)?(10):(11):((5+6)?12:13)))>"
e "<$((1?(3+4)?(10):(11):((5+6)?12:13)))>"
e '-- COND .5'
e "<$((0?3+4?10:11?20+1:22*1:5+6?12:13))>"
e "<$((1?3+4?10:11?20+1:22*1:5+6?12:13))>"
e "<$((0?(3+4)?(10):(11)?(20+1):(22*1):((5+6)?12:13)))>"
e "<$((1?(3+4)?(10):(11)?(20+1):(22*1):((5+6)?12:13)))>"
e '-- COND .6'
e "<$((0?3+4?9:11?20+1:22*1:5+6?12:13))>"
e "<$((1?3+4?9:11?20+1:22*1:5+6?12:13))>"
e "<$((0?10+11?20+1?22*1?23**1:24**1:25/1?26%27:56>>1:-1:-2))>"
e "<$((1?0?20+1?22*1?23**1:24**1:25/1?26%27:56>>1:-1:-2))>"
e "<$((1?10?20+1?22*1?23**1:24**1:25/1?26%27:56>>1:-1:-2))>"
e "<$((1?10?0?22*1?23**1:24**1:25/1?26%27:56>>1:-1:-2))>"
e "<$((1?10?20?0?23**1:24**1:25/1?26%27:56>>1:-1:-2))>"
e "<$((1?10?20?22*1?0:24**1:25/1?26%27:56>>1:-1:-2))>"
e "<$((1?10?20?22*1?23**1:0:25/1?26%27:56>>1:-1:-2))>"
e "<$((1?10?20?22*1?23**1:24**1:0?26%27:56>>1:-1:-2))>"
e "<$((1?10?20?22*1?23**1:24**1:25/1?0:56>>1:-1:-2))>"
e '-- COND .7'
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(( (I1 < I2) ? (I2 < I3) ? I3 *= I3 : (I2 *= I2) : (I1 *= I1)))>";\
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(( (I1 < I2) ? ((I2 < I3) ? I3 *= I3 : (I2 *= I2)) : (I1 *= I1)))>";\
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
   p "<$((((I1<I2)?((I2<I3)?(I3*=I3):(I2*=I2)):(I1*=I1))))>";\
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
p "<$(((I1<I2)?(I2<I3)?(I3<I4)?I4*=I4:(I3*=I3):(I2*=I2):(I1*=I1)))>";\
   e "<$I1><$I2><$I3><$I4><$I5>"
# only first
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(( (I1 < I2) ? (I2 > I3) ? I3 *= I3 : (I2 *= I2) : (I1 *= I1)))>";\
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(((I1<I2)?(I2>I3)?I3*=I3:(I2*=I2):(I1*=I1)))>";\
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(( (I1 < I2) ? ((I2 > I3) ? I3 *= I3 : (I2 *= I2)) : (I1 *= I1)))>";\
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(( ((I1 < I2) ? ((I2 > I3) ? (I3 *= I3):(I2 *= I2)):(I1 *= I1))))>";\
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
p "<$(((I1<I2)?(I2>I3)?(I3>I4)?I4*=I4:(I3*=I3):(I2*=I2):(I1*=I1)))>";\
   e "<$I1><$I2><$I3><$I4>"
# last not etc.
s I1=2 I2=3 I3=4 I4=5;\
p "<$(((I1<I2)?(I2<I3)?(I3>I4)?I4*=I4:(I3*=I3):(I2*=I2):(I1*=I1)))>";\
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
p "<$(((I1<I2)?(I2>I3)?(I3<I4)?I4*=I4:(I3*=I3):(I2*=I2):(I1*=I1)))>";\
   e "<$I1><$I2><$I3><$I4><$I5>"
s I1=2 I2=3 I3=4 I4=5;\
p "<$(((I1>I2)?(I2<I3)?(I3<I4)?I4*=I4:(I3*=I3):(I2*=I2):(I1*=I1)))>";\
   e "<$I1><$I2><$I3><$I4><$I5>"
e '-- COND .8'
s I=0;p "<$((1?I=2:(I=3),8,10))>";e "<$I>"
s I=0;p "<$((1?20:(I+=2)))>";e "<$I>"
s I=0;p "<$((1?I+=10:(I+=2)))>";e "<$I>"
s I=0;p "<$((0?I+=2:20))>";e "<$I>"
s I=0;p "<$((0?I+=2:(I+=10)))>";e "<$I>"
s I=0;p "<$((0?(I+=2):(20)))>";e "<$I>"
s I=0;p "<$((0?(I+=2):(I+=20)))>";e "<$I>"
e '-- COND .9'
s I1=+E+ I2=1+1;p "<$((0?I1:I2))>";e "<$I1><$I2>"
s I1=1+1 I2=+E+;p "<$((1?I1:I2))>";e "<$I1><$I2>"
s I1=+E+ I2=1+1;p "<$((0?I1=1:(I2=2)))>";e "<$I1><$I2>"
s I1=1+1 I2=+E+;p "<$((1?I1=1:(I2=2)))>";e "<$I1><$I2>"
s I1=+E+ I2=1+1;p "<$((0?I1*=I1:(I2*=I2)))>";e "<$I1><$I2>"
s I1=1+1 I2=+E+;p "<$((1?I1*=I1:(I2*=I2)))>";e "<$I1><$I2>"
e '-- COND .10'
s I1=+E+ I2=+E+ I3=+E+ I4=-1;p "<$((0?I1?I2:I3:I4))>";e "<$I1><$I2><$I3><$I4>"
s I1=1 I2=2 I3=+E+ I4=+E+;p "<$((1?I1?I2:I3:I4))>";e "<$I1><$I2><$I3><$I4>"
s I1=0 I2=+E+ I3=3 I4=+E+;p "<$((1?I1?I2:I3:I4))>";e "<$I1><$I2><$I3><$I4>"
e '= WILD I'
e "<$((  3          +           (      11       )  ))>"
e "<$((1 + (2 - 2)))>"
e "<$((1 + (2 - 2)))>"
e "<$(( (( 3 / 3 )) + ((1*1*1)) - (( 7 % 6 ))))>"
e "<$(( 3+((2 * 2))/6 ))>"
e "<$(( 1 + 1 - 3 * 3 + 99-88 / 17))>"
e "<$(( 1 << 2 % 1+2 * 4 - (2 + 2 + 1) * 6 / 7 + 4 * 2 + (81/9)))>"
s I1=I2=10 I2=3;p "<$((I1 + I2))>";e "<$I1><$I2>"
s I1=I2=10 I2=3;p "<$((I1 * I2))>";e "<$I1><$I2>"
s I1=I2=10 I2=3;p "<$((I1 % I2))>";e "<$I1><$I2>"
e '= WILD II'
s I=10;p "<$((3+(3*(I=11))))>";e "<$I>"
s I=10;p "<$((3+(3*(I++))))>";e "<$I>"
s I=10;p "<$((3+(3*(I=11,I++))))>";e "<$I>"
s I=10;p "<$((3+(3*(I=11,++I))))>";e "<$I>"
s I=10;p "<$((3+(3*(I=11,++++I))))>";e "<$I>"
s I=10;p "<$((3+(3*(I=11,+++++++++++++++++++++++-+++++I))))>";e "<$I>"
e "<$((3+(3*(+++++++++++++++++++++++-+++++10))))>"
s I=10;p "<$(( +10 + + +I ))>";e "<$I>"
s I=10;p "<$(( +10 + ++I ))>";e "<$I>"
s I=10;p "<$(( +10 ++ +I ))>";e "<$I>"
s I=10;p "<$(( +10 +++ I ))>";e "<$I>"
s I=10;p "<$(( +10+++I ))>";e "<$I>"
s I=10;p "<$((+10++I))>";e "<$I>"
s I=10;p "<$((+10 + + + ++++ +I))>";e "<$I>"
e "<$(( +10 + + + ++++ +11 ))>"
e "<$(( +10 + + + ++++ ++11 ))>"
e "<$((+10++++++++11))>"
e '= WILD RECUR' # (some yet)
s I1=I2=10 I2=5;p "<$((I1+=I2))>";e "<$I1><$I2>"
s I1=I2=10 I2=5 I3=I2+=1;p "<$((I1))>";e "<$I1><$I2><$I3>"
s I1=I2=10 I2=5 I3=I2+=1;p "<$((I1,I3))>";e "<$I1><$I2><$I3>"
s I1=I2=10 I2=5 I3=I2+=1;p "<$((I1+I3))>";e "<$I1><$I2><$I3>"
s I1=I2=10 I2=5 I3=I2+=1;p "<$((I1?I1:I3))>";e "<$I1><$I2><$I3>"
s I1=I2=0 I2=5 I3=I2+=1;p "<$((I1?I1:I3))>";e "<$I1><$I2><$I3>"
s I1=I1=10 I2=5 I3=I2+=1;p "<$((I1=0?I1:I3))>";e "<$I1><$I2><$I3>"
s I1=I1=10 I2=5 I3=I2+=1;p "<$((I1=1?I1:I3))>";e "<$I1><$I2><$I3>"
s I1=I2='(I2=10)+1' I2=5 I3=I2+=1;p "<$((I1,I3))>";e "<$I1><$I2><$I3>"
s I1=I2='(I2=(I2=10)+1)' I2=5 I3=I2+=1;p "<$((I1,I3))>";e "<$I1><$I2><$I3>"
s I1=I2=10 I2=5 I3=I2+=1;p "<$((I1+I3*I1*I3/I1%I3))>";e "<$I1><$I2><$I3>"
s I1=I2=10 I2=5 I3=I2+=1;p "<$((I1+I3*I1*I3/I1%I3))>";e "<$I1><$I2><$I3>"
s I1=I2=+E+ I2=5;p "<$((I1=10))>";e "<$I1><$I2>"
s I1=I2=+E+ I2=5;p "<$((0?I1:++I2))>";e "<$I1><$I2>"
s I1=I2=10 I2=5;p "<$((I2,(1?I1:++I2)))>";e "<$I1><$I2>"
s I1=5 I2=10 I3=20;p "<$((I1-=5,1?I2:I3))>";e "<$I1><$I2><$I3>"
s I1=5 Ix=6 I2=10 I3=20;p "<$((I1=Ix,1?I2:I3))>";e "<$I1><$I2><$I3>"
s I1=5 Ix=6 I2=10 I3=20;p "<$((I1=Ix?I2:I3))>";e "<$I1><$I2><$I3>"
s I1=5 Ix=6 I2=10 I3=20;p "<$((I1*=Ix?I2:I3))>";e "<$I1><$I2><$I3>"
s I1=5 Ix=6 I2=10 I3=20;p "<$((0,I1*=Ix?I2:I3))>";e "<$I1><$I2><$I3>"
s I1=5 Ix=6 I2=10 I3=20;p "<$((I1*=Ix?I2:I3,Ix=21,I1*=Ix?I2:I3))>";e "<$I1><$I2><$I3>"
	__EOT
   # }}}

   < ./tarith-good.in ${MAILX} ${ARGS} \
      -Y 'commandalias ca \\commandalias' \
      -Y 'ca p \\echon' -Y 'ca e \\echo' -Y 'ca s \\set' \
      > ./tarith-good 2>>${ERR}
   check arith-good 0 ./tarith-good '123218927 6484'

   # $(())-bad {{{
   ${cat} <<- '__EOT' > ./tarith-bad.in
# make this work with (ba)sh \
command -v shopt && shopt -s expand_aliases;\
alias p=printf;alias e=echo;alias s=export
# For sh(1) place some "e"s in follow lines
e '= T0'
e "<$((2 2))>"
e "<$((I1=1 I1=2))>"
e "<$((I1 I1))>"
e '= T1'
e "<$((3425#56))>"
e "<$((7=43))>"
e "<$((2#44))>"
e "<$((44/0))>"
e '= T2'
e "<$((   ,   2    ))>"
e "<$((,2))>"
e "<$((2,))>"
e "<$((1,,2))>"
e '= T3'
e "<$((3+(3*I=11)))>"
e "<$((3+(3*=11)))>"
e "<$((3+(3=11)))>"
e "<$((3+(=11)))>"
e '= T4'
e "<$((?))>"
e "<$((0?))>"
e "<$((?1))>"
e "<$((1?))>"
e "<$((1?2))>"
e "<$((:))>"
e "<$((1:))>"
e "<$((:2))>"
e "<$((1:2))>"
e "<$((0 ? 0 ? 0 : 1 : 2 : 3))>"
e "<$((0 ? 0 ? 0 : 1 :))>"
e "<$((0 ? 0 ? 0 : 1))>"
e '= T5'
s I='1 + 1';p "<$((++I))>"
   e "<$I>"
s I='1 + 1 +';p "<$((I))>"
   e "<$I>"
s I='1 + 1';p "<$((I+))>"
   e "<$I>"
e '= T6'
s I=10;p "<$((++I=3))>"
   e "<$I>"
s I=10;p "<$((--I=3))>"
   e "<$I>"
s I=10;p "<$(((++I)=3))>"
   e "<$I>"
s I=10;p "<$(((--I)=3))>"
   e "<$I>"
s I=1 J=10;p "<$(((J)++*(I)++))>"
   e "<$I><$J>"
s I=1 J=10;p "<$(((J)--*(I)--))>"
   e "<$I><$J>"
e "<$((3+(3*(I=11)++)))>"
e "<$((3+(3*((I=11)++))))>"
e '= T7'
s I1=2 I2=3 I3=4;\
   p "<$((I1<I2?I3*=I3:I2*=I2))>"
   e "0<$I1><$I2><$I3>"
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(((I1<I2)?(I2<I3)?I3*=I3:I2*=I2:I1*=I1))>"
   e "1<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5 I5=6;\
   p "<$(((I1<I2)?(I2>I3)?(I3<I4)?(I4<I5)?I5*=I5:I4*=I4:I3*=I3:I2*=I2:I1*=I1))>"
   e "2<$I1><$I2><$I3><$I4><$I5>"
s I1=2 I2=3 I3=4 I4=5 I5=6;\
   p "<$(((I1<I2)?(I2>I3)?(I3<I4)?(I4<I5)?(I5*=I5):I4*=I4:I3*=I3:I2*=I2:I1*=I1))>"
   e "3<$I1><$I2><$I3><$I4><$I5>"
s I1=2 I2=3 I3=4 I4=5 I5=6;\
   p "<$(((I1<I2)?(I2>I3)?(I3<I4)?(I4<I5)?(I5*=I5):(I4*=I4):I3*=I3:I2*=I2:I1*=I1))>"
   e "4<$I1><$I2><$I3><$I4><$I5>"
s I1=2 I2=3 I3=4 I4=5 I5=6;\
   p "<$(((I1<I2)?(I2>I3)?(I3<I4)?(I4<I5)?(I5*=I5):(I4*=I4):(I3*=I3):I2*=I2:I1*=I1))>"
   e "5<$I1><$I2><$I3><$I4><$I5>"
s I1=2 I2=3 I3=4 I4=5 I5=6;\
   p "<$(((I1<I2)?(I2>I3)?(I3<I4)?(I4<I5)?(I5*=I5):(I4*=I4):(I3*=I3):(I2*=I2):I1*=I1))>"
   e "6<$I1><$I2><$I3><$I4><$I5>"
s I1=2 I2=3 I3=4 I4=5 I5=6;\
   p "<$(((I1<I2)?(I2>I3)?(I3<I4)?(I4<I5)?I5*=I5:I4*=I4:(I3*=I3):(I2*=I2):(I1*=I1)))>"
   e "7<$I1><$I2><$I3><$I4><$I5>"
s I1=2 I2=3 I3=4 I4=5 I5=6;\
   p "<$(((I1<I2)?(I2>I3)?(I3<I4)?(I4<I5)?I5*=I5:I4*=I4:I3*=I3:(I2*=I2):(I1*=I1)))>"
   e "8<$I1><$I2><$I3><$I4><$I5>"
s I1=2 I2=3 I3=4 I4=5 I5=6;\
   p "<$(((I1<I2)?(I2>I3)?(I3<I4)?(I4<I5)?I5*=I5:I4*=I4:I3*=I3:I2*=I2:(I1*=I1)))>"
   e "9<$I1><$I2><$I3><$I4><$I5>"
e '= T8'
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(( (I1 < I2) ? ((I2 > I3) ? I3 *= I3 : I2 *= I2) : (I1 *= I1)))>"
   e "<$I1><$I2><$I3><$I4>"
s I1=2 I2=3 I3=4 I4=5;\
   p "<$(( (I1 < I2) ? ((I2 > I3) ? I3 *= I3 : (I2 *= I2)) : I1 *= I1))>"
   e "<$I1><$I2><$I3><$I4>"
e '= T9'
s I1=I2=+E+ I2=5;p "<$((I1))>"
   e "<$I1><$I2>"
s I1=I2=+E+ I2=5;p "<$((1?I1:++I2))>"
   e "<$I1><$I2>"
s I1=I2=+E+ I2=5;p "<$((I2,(1?I1:++I2)))>"
   e "<$I1><$I2>"
s I1=+E+ I2=6 I3=+E+;p "<$((0?I1=10:(0?I3:I2=12)))>"
   e "<$I1><$I2><$I3>"
s I1=+E+ I2=+E+ I3=7;p "<$((0?I1=10:(1?I3:I2=12)))>"
   e "<$I1><$I2><$I3>"
e '= T10'
s I1=+E+ I2=1+1;p "<$((1?I1:I2))>";e "<$I1><$I2>"
s I1=1+1 I2=+E+;p "<$((0?I1:I2))>";e "<$I1><$I2>"
s I1=+E+ I2=1+1;p "<$((1?I1+=1:(I2=2)))>";e "<$I1><$I2>"
e '= T11'
s I1=1 I2=2 I3=3 I4=+E+;p "<$((0?I1?I2:I3:I4))>";e "<$I1><$I2><$I3><$I4>"
s I1=+E+ I2=2 I3=3 I4=4;p "<$((1?I1?I2:I3:I4))>";e "<$I1><$I2><$I3><$I4>"
s I1=1 I2=+E+ I3=3 I4=4;p "<$((1?I1?I2:I3:I4))>";e "<$I1><$I2><$I3><$I4>"
s I1=0 I2=2 I3=+E+ I4=4;p "<$((1?I1?I2:I3:I4))>";e "<$I1><$I2><$I3><$I4>"
	__EOT
   # }}}

   < ./tarith-bad.in ${MAILX} ${ARGS} \
      -Y 'commandalias ca \\commandalias' \
      -Y 'ca p \\echon' -Y 'ca e \\echo' -Y 'ca s \\set' \
      > ./tarith-bad 2>>${ERR}
   check arith-bad 0 ./tarith-bad '1862980575 467'

   </dev/null ${MAILX} ${ARGS} -Y '
      define x {
         \local :$((myvar=1+1))
         \vars myvar
      }
      \vars myvar
      \call x
      \vars myvar
      \xit
   ' > ./tarith-local 2>&1
   check arith-local 0 ./tarith-local '455497105 40'

   t_epilog "${@}"
} # }}}

t_commandalias() { # {{{
   t_prolog "${@}"

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1
	commandalias echo echo hoho
	echo stop.
	commandalias X Xx
	commandalias Xx XxX
	commandalias XxX XxXx
	commandalias XxXx XxXxX
	commandalias XxXxX XxXxXx
	commandalias XxXxXx echo huhu
	commandalias XxXxXxX echo huhu
	X
	commandalias XxXxXx XxXxXxX
	X
	uncommandalias echo
	commandalias XxXxXx echo huhu
	X
	__EOT

   check 1 0 ./t1 '1638809585 36'

   t_epilog "${@}"
} # }}}

t_posix_abbrev() { # {{{
   t_prolog "${@}"

   #{{{ In POSIX C181 standard order
   </dev/null ${MAILX} ${ARGS} \
      -Y 'echon alias/a\ ; ? a; echon group/g\ ; ?g' \
      -Y 'echon alternates/alt\ ; ? alt' \
      -Y 'echon chdir/ch\ ; ? ch' \
      -Y 'echon copy/c\ ; ? c; echon Copy/C\ ; ?C' \
      -Y 'echon delete/d\ ; ? d' \
      -Y 'echon discard/di\ ; ? di; echon ignore/ig\ ; ?ig' \
      -Y 'echon echo/ec\ ; ? ec' \
      -Y 'echon edit/e\ ; ? e' \
      -Y 'echon exit/ex\ ; ? ex; echon xit/x\ ; ?x' \
      -Y 'echon file/fi\ ; ? fi; echon folder/fold\ ; ?  fold' \
      -Y 'echon followup/fo\ ; ? fo; echon Followup/F\ ; ?F' \
      -Y 'echon from/f\ ; ? f' \
      -Y 'echon headers/h\ ; ? h' \
      -Y 'echon help/hel\ ; ? hel' \
      -Y 'echon hold/ho\ ; ? ho; echon preserve/pre\ ; ? pre' \
      -Y 'echon if/i\ ; ? i; echon else/el\ ; ? el; echon endif/en\ ; ? en' \
      -Y 'echon list/l\ ; ? l' \
      -Y 'echon mail/m\ ; ? m' \
      -Y 'echon mbox/mb\ ; ? mb' \
      -Y 'echon next/n\ ; ? n' \
      -Y 'echon pipe/pi\ ; ? pi' \
      -Y 'echon Print/P\ ; ? P; echon Type/T\ ; ? T' \
      -Y 'echon print/p\ ; ? p; echon type/t\ ; ? t' \
      -Y 'echon quit/q\ ; ? q' \
      -Y 'echon Reply/R\ ; ? R' \
      -Y 'echon reply/r\ ; ? r' \
      -Y 'echon retain/ret\ ; ? ret' \
      -Y 'echon save/s\ ; ? s; echon Save/S\ ; ? S' \
      -Y 'echon set/se\ ; ? se' \
      -Y 'echon shell/sh\ ; ? sh' \
      -Y 'echon size/si\ ; ? si' \
      -Y 'echon source/so\ ; ? so' \
      -Y 'echon touch/tou\ ; ? tou' \
      -Y 'echon unalias/una\ ; ? una' \
      -Y 'echon undelete/u\ ; ? u' \
      -Y 'echon unset/uns\ ; ? uns' \
      -Y 'echon visual/v\ ; ? v' \
      -Y 'echon write/w\ ; ? w' \
      | ${sed} -e 's/:.*$//' > ./t1
   #}}}
   check 1 0 ./t1 '1012680481 968'

   t_epilog "${@}"
} # }}}
# }}}

# Basics {{{
t_shcodec() { # {{{
   t_prolog "${@}"

   #{{{ XXX the first needs to be checked, it is quite dumb as such
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
	commandalias x echo '$?/$^ERRNAME'
	shcodec e abcd
	x
	shcodec d abcd
	x
	shcodec e a'b'c'd'
	x
	shcodec d a'b'c'd'
	x
	shcodec e a"b"c"d"
	x
	shcodec d a"b"c"d"
	x
	shcodec e a$'b'c$'d'
	x
	shcodec d a$'b'c$'d'
	x
	shcodec e 'abcd'
	x
	shcodec d 'abcd'
	x
	shcodec e "abcd"
	x
	shcodec d "abcd"
	x
	shcodec e $'abcd'
	x
	shcodec d $'abcd'
	x
	# same but with vput
	commandalias y echo '$?/$^ERRNAME $res'
	vput shcodec res e abcd
	y
	eval shcodec d $res
	x
	vput shcodec res d abcd
	y
	eval shcodec d $res
	x
	vput shcodec res e a'b'c'd'
	y
	eval shcodec d $res
	x
	vput shcodec res d a'b'c'd'
	y
	eval shcodec d $res
	x
	vput shcodec res e a"b"c"d"
	y
	eval shcodec d $res
	x
	vput shcodec res d a"b"c"d"
	y
	eval shcodec d $res
	x
	vput shcodec res e a$'b'c$'d'
	y
	eval shcodec d $res
	x
	vput shcodec res d a$'b'c$'d'
	y
	eval shcodec d $res
	x
	vput shcodec res e 'abcd'
	y
	eval shcodec d $res
	x
	vput shcodec res d 'abcd'
	y
	eval shcodec d $res
	x
	vput shcodec res e "abcd"
	y
	eval shcodec d $res
	x
	vput shcodec res d "abcd"
	y
	eval shcodec d $res
	x
	vput shcodec res e $'abcd'
	y
	eval shcodec d $res
	x
	vput shcodec res d $'abcd'
	y
	eval shcodec d $res
	x
	#
	vput shcodec res e a b\ c d
	y
	eval shcodec d $res
	x
	vput shcodec res d a b\ c d
	y
	vput shcodec res e ab cd
	y
	eval shcodec d $res
	x
	vput shcodec res d 'ab cd'
	y
	vput shcodec res e a 'b c' d
	y
	eval shcodec d $res
	x
	vput shcodec res d a 'b c' d
	y
	vput shcodec res e a "b c" d
	y
	eval shcodec d $res
	x
	vput shcodec res d a "b c" d
	y
	vput shcodec res e a $'b c' d
	y
	eval shcodec d $res
	x
	vput shcodec res d a $'b c' d
	y
	#
	vput shcodec res e 'a$`"\'
	y
	eval shcodec d $res
	x
	vput shcodec res d 'a$`"\'
	y
	vput shcodec res e "a\$\`'\"\\"
	y
	eval shcodec d $res
	x
	vput shcodec res d "a\$\`'\"\\"
	y
	vput shcodec res e $'a\$`\'\"\\'
	y
	eval shcodec d $res
	x
	vput shcodec res d $'a\$`\'\"\\'
	y
	vput shcodec res e $'a\$`\'"\\'
	y
	eval shcodec d $res
	x
	vput shcodec res d $'a\$`\'"\\'
	y
	#
	set diet=curd
	vput shcodec res e a${diet}c
	y
	eval shcodec d $res
	x
	eval vput shcodec res e a${diet}c
	y
	eval shcodec d $res
	x
	vput shcodec res e "a${diet}c"
	y
	eval shcodec d $res
	x
	eval vput shcodec res e "a${diet}c"
	y
	eval shcodec d $res
	x
	__EOT
   #}}}
   check 1 0 ./t1 '3316745312 1241'

   if [ -z "${UTF8_LOCALE}" ]; then
      t_echoskip 'unicode:[no UTF-8 locale]'
   elif have_feat multibyte-charsets; then
      #{{{
      ${cat} <<- '__EOT' | LC_ALL=${UTF8_LOCALE} \
         ${MAILX} ${ARGS} > ./tunicode 2>>${ERR}
		#
		shcodec e täst
		shcodec +e täst
		shcodec d $'t\u00E4st'
		shcodec e aՍc
		shcodec +e aՍc
		shcodec d $'a\u054Dc'
		shcodec e a𝕂c
		shcodec +e a𝕂c
		shcodec d $'a\U0001D542c'
		__EOT
      #}}}
      check unicode 0 ./tunicode '1175985867 77'
   else
      t_echoskip 'unicode:[!MULTIBYTE-CHARSETS]'
   fi

   t_epilog "${@}"
} # }}}

t_ifelse() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./teasy 2>>${ERR}
   commandalias e \\echo
   set i=0
   if [ $i -eq 0 ]
      e 0
      set i=1
   eli [ $i -ne 0 ]
      e 1
      set i=2
   els
      set i=3
      e 2
   end
   e =$i
   set i=10
   if [ $i -eq 0 ]
      e 0
      set i=1
   eli [ $i -ne 0 ]
      e 1
      set i=2
   els
      set i=3
      e 2
   end
   e =$i
   set i=10
   if [ $i -eq 0 ]
      e 0
      set i=1
   eli [ $i -ne 10 ]
      e 1
      set i=2
   els
      set i=3
      e 2
   end
   e =$i
   #
   set i=0
   if 1
      if 0
         if [ $i -eq 0 ]
            e 0
            set i=1
         eli [ $i -ne 0 ]
            e 1
            set i=2
         els
            set i=3
            e 2
         end
         e =$i
      end
   end
   e ==$i
   set i=10
   if 1
      if 0
         set i=5
         define x {
            #
         }
      els
         set i=10
         if [ $i -eq 0 ]
            e 0
            set i=1
            define x {
               #
            }
         eli [ $i -ne 0 ]
            e 1
            set i=2
            define x {
               #
            }
         els
            set i=3
            e 2
            define x {
               #
            }
         end
         define x {
            #
         }
         e =$i
      end
   end
   e ==$i
	__EOT
   #}}}
   check easy 0 ./teasy '1243020683 28'

   # {{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tshexpign 2>>${ERR}
   commandalias e \\echo
   set i=0
   if [ $((i+=10)) -eq 10 ]
      e 0
   eli [ $((++i)) -eq 11 ]
      e 1
   els
      :$((i+=40))
      e 2
   end
   e =$i
   set i=0
   if [ $((i+=10)) -eq 1 ]
      e 0
   eli [ $((++i)) -eq 11 ]
      e 1
   els
      :$((i+=40))
      e 2
   end
   e =$i
   set i=0
   if [ $((i+=10)) -eq 1 ]
      e 0
   eli [ $((i++)) -eq 11 ]
      e 1
   els
      :$((i+=40))
      e 2
   end
   e =$i
   #
   set i=0
   if 1
      if 0
         if [ $((i+=10)) -eq 10 ]
            e 0
         eli [ $((++i)) -eq 11 ]
            e 1
         els
            :$((i+=40))
            e 2
         end
      end
   end
   e =$i
   set i=0
   if 1
      if 0
         :$((i=5))
      els
         if [ $((i+=10)) -eq 10 ]
            e 0
         eli [ $((++i)) -eq 11 ]
            e 1
         els
            :$((i+=40))
            e 2
         end
      end
   end
   e =$i
	__EOT
   #}}}
   check shexpign 0 ./tshexpign '1174905124 27'

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tbadsyn 2>>${ERR}
   if !;echo 1a;else;echo 1b;endif
   if [ ! && a == hey ];echo 2a;else;echo 2b;endif
	__EOT
   check badsyntax 0 ./tbadsyn '4294967295 0'

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tgoodsyn 2>>${ERR}
   commandalias e \\echo
   commandalias f \\endif
   if ! false;e y1.1;f
   if [ ! != x ];e y1.2;f
   if [ ! == ! ];e y1.3;f
   if [ & == & ];e y1.4;f
   if [ | == | ];e y1.5;f
   if [ & != | ];e y1.6;f
   if [ | != & ];e y1.7;f
   #
   if [ && == && ];e y1.8;f
   if [ || == || ];e y1.9;f
   if [ && != || ];e y1.10;f
   if [ || != && ];e y1.11;f
   #
   if [ == [;e y1.12;f
   if ! ! [ == [;e y1.13;f
   if ! [ != [;e y1.14;f
   if [ [ == [ ];e y1.15;f
   if [ ! ! [ == [ ];e y1.16;f
   if [ ! [ != [ ];e y1.16;f
   if [ [ [ [ == [ ] ] ];e y1.17;f
   #
   if ] == ];e y1.18;f
   if ! ! ] == ];e y1.19;f
   if ! ] != ];e y1.20;f
   if [ ] == ] ];e y1.21;f
   if [ ! ! ] == ] ];e y1.22;f
   if [ ! ] != ] ];e y1.23;f
   if [ [ [ ] == ] ] ] ];e y1.24;f
   ##
   if ! false;e y2.1;f
   if ! != x;e y2.2;f
   if ! ! ! != x;e y2.3;f
   if ! == !;e y2.4;f
   if & == &;e y2.5;f
   if | == |;e y2.6;f
   if & != |;e y2.7;f
   if | != &;e y2.8;f
   #
   if && == &&;e y2.9;f
   if || == ||;e y2.10;f
   if && != ||;e y2.11;f
   if || != &&;e y2.12;f
   if ! && == &&;e y2.13;f
   if ! || == ||;e y2.14;f
   if ! && != ||;e y2.15;f
   if ! || != &&;e y2.16;f
   ##
   if [ ! false ];e y2.20;f
   if [ ! != x ];e y2.21;f
   if ! [ ! ! != x ] ;e y2.22;f
   if [ ! == ! ];e y2.23;f
   if [ & == & ];e y2.24;f
   if [ | == | ];e y2.25;f
   if [ & != | ];e y2.26;f
   if [ | != & ];e y2.27;f
   #
   if [ && == && ];e y2.28;f
   if [ || == || ];e y2.29;f
   if [ && != || ];e y2.30;f
   if [ || != && ];e y2.31;f
   if ! [ && == && ];e y2.32;f
   if [ ! [ || == || ] ];e y2.33;f
   if [ ! && != || ];e y2.34;f
   if [ ! || != && ];e y2.35;f
   ##
   if [ && == && ] && [ && == && ];e y3.1;f
   if [ || == || ] || [ || == || ];e y3.2;f
   if [ && != || ] && [ && != || ];e y3.3;f
   if [ || != && ] || [ || != && ];e y3.4;f
   #
   if && == && && && == &&;e y4.1;f
   if || == || || || == ||;e y4.2;f
   if && != || && && != ||;e y4.3;f
   if || != && || || != &&;e y4.4;f
   ##
   if [ '' -lt 1 ];e y10.1;f
   ## (examplary: binaries are all alike in our eyes)
   if == == ==;e y20.1;f
   if == != !=;e y20.2;f
   if != == !=;e y20.3;f
   if != != ==;e y20.4;f
   #
   if [ == == == ];e y21.1;f
   if [ == != != ];e y21.2;f
   if [ != == != ];e y21.3;f
   if [ != != == ];e y21.4;f
   #
   if ! == == ==;else;e y22.1;f
   if ! == != !=;else;e y22.2;f
   if ! != == !=;else;e y22.3;f
   if ! != != ==;else;e y22.4;f
   #
   if ! [ == == == ];else;e y23.1;f
   if ! [ == != != ];else;e y23.2;f
   if ! [ != == != ];else;e y23.3;f
   if ! [ != != == ];else;e y23.4;f
   #
   if [ ! == == == ];else;e y24.1;f
   if [ ! == != != ];else;e y24.2;f
   if [ ! != == != ];else;e y24.3;f
   if [ ! != != == ];else;e y24.4;f
   ##
   if -n [;e y30.1;f
   if -n ];e y30.2;f
   if -n &&;e y30.3;f
   if -n ||;e y30.4;f
   if ! -z [;e y31.1;f
   if ! -z ];e y31.2;f
   if ! -z &&;e y31.3;f
   if ! -z ||;e y31.4;f
	__EOT
   #}}}
   check goodsyntax 0 ./tgoodsyn '2169678278 550'

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tNnZz_whiteout
	\if -N xyz; echo 1.err-1; \
		\elif ! -Z xyz;echo 1.err-2;\
		\elif -n "$xyz"     ;      echo 1.err-3   ;    \
		\elif ! -z "$xyz"     ;      echo 1.err-4   ;    \
		\else;echo 1.ok;\
		\end
	\set xyz
	\if ! -N xyz; echo 2.err-1; \
		\elif -Z xyz;echo 2.err-2;\
		\elif -n "$xyz"     ;      echo 2.err-3   ;    \
		\elif ! -z "$xyz"     ;      echo 2.err-4   ;    \
		\else;echo 2.ok;\
		\end
	\set xyz=notempty
	\if ! -N xyz; echo 3.err-1; \
		\elif -Z xyz;echo 3.err-2;\
		\elif ! -n "$xyz";echo 3.err-3;\
		\elif -z "$xyz";echo 3.err-4;\
		\else;echo 3.ok;\
		\end
	\if $xyz != notempty;echo 4.err-1;else;echo 4.ok;\end
	\if $xyz == notempty;echo 5.ok;else;echo 5.err-1;\end
	__EOT
   check NnZz_whiteout 0 ./tNnZz_whiteout '4280687462 25'

   #{{{ # TODO t_ifelse: individual tests as for NnZz_whiteout
   # Nestable conditions test
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tnormal
		if 0
		   echo 1.err
		else
		   echo 1.ok
		endif
		if 1
		   echo 2.ok
		else
		   echo 2.err
		endif
		if [ "$dietcurd" != "" ]
		   echo 3.err
		else
		   echo 3.ok
		endif
		set dietcurd=yoho
		if $'\$dietcurd' != ""
		   echo 4.ok
		else
		   echo 4.err
		endif
		if "$dietcurd" == 'yoho'
		   echo 5.ok
		else
		   echo 5.err
		endif
		if $'\$dietcurd' ==? 'Yoho'
			echo 5-1.ok
		else
			echo 5-1.err
		endif
		if $dietcurd == 'Yoho'
			echo 5-2.err
		else
			echo 5-2.ok
		endif
		if $dietcurd != 'yoho'
		   echo 6.err
		else
		   echo 6.ok
		endif
		if $dietcurd !=?case 'Yoho'
			echo 6-1.err
		else
			echo 6-1.ok
		endif
		if $dietcurd != 'Yoho'
			echo 6-2.ok
		else
			echo 6-2.err
		endif
		# Nesting
		if faLse
		   echo 7.err1
		   if tRue
		      echo 7.err2
		      if yEs
		         echo 7.err3
		      else
		         echo 7.err4
		      endif
		      echo 7.err5
		   endif
		   echo 7.err6
		else
		   echo 7.ok7
		   if YeS
		      echo 7.ok8
		      if No
		         echo 7.err9
		      else
		         echo 7.ok9
		      endif
		      echo 7.ok10
		   else
		      echo 7.err11
		      if yeS
		         echo 7.err12
		      else
		         echo 7.err13
		      endif
		   endif
		   echo 7.ok14
		endif
		if r
		   echo 8.ok1
		   if R
		      echo 8.ok2
		   else
		      echo 8.err2
		   endif
		   echo 8.ok3
		else
		   echo 8.err1
		endif
		if s
		   echo 9.err1
		else
		   echo 9.ok1
		   if S
		      echo 9.err2
		   else
		      echo 9.ok2
		   endif
		   echo 9.ok3
		endif
		# `elif'
		if $dietcurd == 'yohu'
		   echo 10.err1
		elif $dietcurd == 'yoha'
		   echo 10.err2
		elif $dietcurd == 'yohe'
		   echo 10.err3
		elif $dietcurd == 'yoho'
		   echo 10.ok1
		   if $dietcurd == 'yohu'
		      echo 10.err4
		   elif $dietcurd == 'yoha'
		      echo 10.err5
		   elif $dietcurd == 'yohe'
		      echo 10.err6
		   elif $dietcurd == 'yoho'
		      echo 10.ok2
		      if $dietcurd == 'yohu'
		         echo 10.err7
		      elif $dietcurd == 'yoha'
		         echo 10.err8
		      elif $dietcurd == 'yohe'
		         echo 10.err9
		      elif $dietcurd == 'yoho'
		         echo 10.ok3
		      else
		         echo 10.err10
		      endif
		   else
		      echo 10.err11
		   endif
		else
		   echo 10.err12
		endif
		# integer
		set dietcurd=10
		if $dietcurd -lt 11
		   echo 11.ok1
		   if $dietcurd -gt 9
		      echo 11.ok2
		   else
		      echo 11.err2
		   endif
		   if $dietcurd -eq 10
		      echo 11.ok3
		   else
		      echo 11.err3
		   endif
		   if $dietcurd -ge 10
		      echo 11.ok4
		   else
		      echo 11.err4
		   endif
		   if $dietcurd -le 10
		      echo 11.ok5
		   else
		      echo 11.err5
		   endif
		   if $dietcurd -ge 11
		      echo 11.err6
		   else
		      echo 11.ok6
		   endif
		   if $dietcurd -ge?satu -0xFFFFFFFFFFFFFFFF1
		      echo 11.err7
		   else
		      echo 11.ok7
		   endif
		else
		   echo 11.err1
		endif
		set dietcurd=Abc
		if $dietcurd < aBd
		   echo 12.ok1
		   if $dietcurd >? abB
		      echo 12.ok2
		   else
		      echo 12.err2
		   endif
		   if $dietcurd ==?case aBC
		      echo 12.ok3
		   else
		      echo 12.err3
		   endif
		   if $dietcurd >=?ca AbC
		      echo 12.ok4
		   else
		      echo 12.err4
		   endif
		   if $dietcurd <=? ABc
		      echo 12.ok5
		   else
		      echo 12.err5
		   endif
		   if $dietcurd >=?case abd
		      echo 12.err6
		   else
		      echo 12.ok6
		   endif
		   if $dietcurd <=? abb
		      echo 12.err7
		   else
		      echo 12.ok7
		   endif
		else
		   echo 12.err1
		endif
		if $dietcurd < aBc
		   echo 12-1.ok
		else
		   echo 12-1.err
		endif
		if $dietcurd <? aBc
		   echo 12-2.err
		else
		   echo 12-2.ok
		endif
		if $dietcurd > ABc
		   echo 12-3.ok
		else
		   echo 12-3.err
		endif
		if $dietcurd >? ABc
		   echo 12-3.err
		else
		   echo 12-3.ok
		endif
		if $dietcurd =%?case aB
		   echo 13.ok
		else
		   echo 13.err
		endif
		if $dietcurd =% aB
		   echo 13-1.err
		else
		   echo 13-1.ok
		endif
		if $dietcurd =%? bC
		   echo 14.ok
		else
		   echo 14.err
		endif
		if $dietcurd !% aB
		   echo 15-1.ok
		else
		   echo 15-1.err
		endif
		if $dietcurd !%? aB
		   echo 15-2.err
		else
		   echo 15-2.ok
		endif
		if $dietcurd !% bC
		   echo 15-3.ok
		else
		   echo 15-3.err
		endif
		if $dietcurd !%? bC
		   echo 15-4.err
		else
		   echo 15-4.ok
		endif
		if $dietcurd =% Cd
		   echo 16.err
		else
		   echo 16.ok
		endif
		if $dietcurd !% Cd
		   echo 17.ok
		else
		   echo 17.err
		endif
		set diet='ab c' curd='ab c'
		if "$diet" == "$curd"
		   echo 18.ok
		else
		   echo 18.err
		endif
		set diet='ab c' curd='ab cd'
		if "$diet" != "$curd"
		   echo 19.ok
		else
		   echo 19.err
		endif
		# 1. Shitty grouping capabilities as of today
		unset diet curd ndefined
		if [ [ false ] || [ false ] || [ true ] ] && \
		      [ [ false ] || [ true ] ] && \
		      [ yes ]
		   echo 20.ok
		else
		   echo 20.err
		endif
		if [ [ [ [ 0 ] || [ 1 ] ] && [ [ 1 ] || [ 0 ] ] ] && [ 1 ] ] && [ yes ]
		   echo 21.ok
		else
		   echo 21.err
		endif
		if [ [ 1 ] || [ 0 ] || [ 0 ] || [ 0 ] ]
		   echo 22.ok
		else
		   echo 22.err
		endif
		if [ [ 1 ] || [ 0 ] || [ 0 ] || [ 0 ] || [ [ 1 ] ] ]
		   echo 23.ok
		else
		   echo 23.err
		endif
		if [ [ 1 ] || [ 0 ] || [ 0 ] || [ 0 ] || [ [ 1 ] ] || [ 1 ] ] && [ no ]
		   echo 24.err
		else
		   echo 24.ok
		endif
		if [ [ 1 ] || [ 0 ] || [ 0 ] || [ 0 ] || [ [ 1 ] ] || [ 1 ] ] \
		      && [ no ] || [ yes ]
		   echo 25.ok
		else
		   echo 25.err
		endif
		if [ [ [ [ [ [ [ 1 ] ] && [ 1 ] ] && [ 1 ] ] && [ 1 ] ] ] && [ 1 ] ]
		   echo 26.ok
		else
		   echo 26.err
		endif
		if [ [ [ [ [ [ [ 1 ] ] && [ 1 ] ] && [ 1 ] ] && [ 1 ] ] ] && [ 0 ] ]
		   echo 27.err
		else
		   echo 27.ok
		endif
		if [ [ [ [ [ [ [ 1 ] ] && [ 1 ] ] && [ 0 ] ] && [ 1 ] ] ] && [ 1 ] ]
		   echo 28.err
		else
		   echo 28.ok
		endif
		if [ [ [ [ [ [ [ 0 ] ] && [ 1 ] ] && [ 1 ] ] && [ 1 ] ] ] && [ 1 ] ]
		   echo 29.err
		else
		   echo 29.ok
		endif
		if [ 1 ] || [ 0 ] || [ 0 ] || [ 0 ] && [ 0 ]
		   echo 30.err
		else
		   echo 30.ok
		endif
		if [ 1 ] || [ 0 ] || [ 0 ] || [ 0 ] && [ 1 ]
		   echo 31.ok
		else
		   echo 31.err
		endif
		if [ 0 ] || [ 0 ] || [ 0 ] || [ 1 ] && [ 0 ]
		   echo 32.err
		else
		   echo 32.ok
		endif
		if [ 0 ] || [ 0 ] || [ 0 ] || [ 1 ] && [ 1 ]
		   echo 33.ok
		else
		   echo 33.err
		endif
		if [ 0 ] || [ 0 ] || [ 0 ] || [ 1 ] && [ 0 ] || [ 1 ] && [ 0 ]
		   echo 34.err
		else
		   echo 34.ok
		endif
		if [ 0 ] || [ 0 ] || [ 0 ] || [ 1 ] && [ 0 ] || [ 1 ] && [ 1 ]
		   echo 35.ok
		else
		   echo 35.err
		endif
		set diet=yo curd=ho
		if [ [ $diet == 'yo' ] && [ $curd == 'ho' ] ] && \
		      [ -N ndefined || -n "$ndefined" || \
		      ! -Z ndefined || ! -z "$ndefined" ]
		   echo 36.err
		else
		   echo 36.ok
		endif
		set ndefined
		if [ [ $diet == 'yo' ] && [ $curd == 'ho' ] ] && \
		      -N ndefined && ! -n "$ndefined" && \
		      ! -Z ndefined && -z "$ndefined"
		   echo 37.ok
		else
		   echo 37.err
		endif
		# 2. Shitty grouping capabilities as of today
		unset diet curd ndefined
		if [ false || false || true ] && [ false || true ] && yes
		   echo 40.ok
		else
		   echo 40.err
		endif
		if [ [ [ 0 || 1 ] && [ 1 || 0 ] ] && 1 ] && [ yes ]
		   echo 41.ok
		else
		   echo 41.err
		endif
		if [ 1 || 0 || 0 || 0 ]
		   echo 42.ok
		else
		   echo 42.err
		endif
		if [ 1 || 0 || 0 || 0 || [ 1 ] ]
		   echo 43.ok
		else
		   echo 43.err
		endif
		if [ 1 || 0 || 0 || 0 || [ 1 ] || 1 ] && no
		   echo 44.err
		else
		   echo 44.ok
		endif
		if [ 1 || 0 || 0 || 0 || 1 || [ 1 ] ] && no || [ yes ]
		   echo 45.ok
		else
		   echo 45.err
		endif
		if [ [ [ [ [ [ 1 ] && 1 ] && 1 ] && 1 ] ] && [ 1 ] ]
		   echo 46.ok
		else
		   echo 46.err
		endif
		if [ [ [ [ [ [ 1 ] && 1 ] && 1 ] && [ 1 ] ] ] && 0 ]
		   echo 47.err
		else
		   echo 47.ok
		endif
		if [ [ [ [ [ [ [ 1 ] ] && 1 ] && 0 ] && [ 1 ] ] ] && 1 ]
		   echo 48.err
		else
		   echo 48.ok
		endif
		if [ [ [ [ [ [ 0 ] && 1 ] && 1 ] && 1 ] ] && 1 ]
		   echo 49.err
		else
		   echo 49.ok
		endif
		if 1 || 0 || 0 || 0 && 0
		   echo 50.err
		else
		   echo 50.ok
		endif
		if 1 || 0 || 0 || 0 && 1
		   echo 51.ok
		else
		   echo 51.err
		endif
		if 0 || 0 || 0 || 1 && 0
		   echo 52.err
		else
		   echo 52.ok
		endif
		if 0 || 0 || 0 || 1 && 1
		   echo 53.ok
		else
		   echo 53.err
		endif
		if 0 || 0 || 0 || 1 && 0 || 1 && 0
		   echo 54.err
		else
		   echo 54.ok
		endif
		if 0 || 0 || 0 || 1 && 0 || 1 && 1
		   echo 55.ok
		else
		   echo 55.err
		endif
		set diet=yo curd=ho
		if [ $diet == 'yo' && $curd == 'ho' ] && \
		      [ -N ndefined || -n "$ndefined" || \
		         ! -Z ndefined || ! -z "$ndefined" ]
		   echo 56.err
		else
		   echo 56.ok
		endif
		if [ $diet == 'yo' && $curd == 'ho' && \
		      [ [ -N ndefined || -n "$ndefined" || \
		         ! -Z ndefined || ! -z "$ndefined" ] ] ]
		   echo 57.err
		else
		   echo 57.ok
		endif
		set ndefined
		if [ $diet == 'yo' && $curd == 'ho' ] && \
		      -N ndefined && ! -n "$ndefined" && \
		      ! -Z ndefined && -z "$ndefined"
		   echo 57.ok
		else
		   echo 57.err
		endif
		if $diet == 'yo' && $curd == 'ho' && ! -Z ndefined
		   echo 58.ok
		else
		   echo 58.err
		endif
		if [ [ [ [ [ [ $diet == 'yo' && $curd == 'ho' && -N ndefined ] ] ] ] ] ]
		   echo 59.ok
		else
		   echo 59.err
		endif
		# Some more en-braced variables
		set diet=yo curd=ho
		if ${diet} == ${curd}
		   echo 70.err
		else
		   echo 70.ok
		endif
		if "${diet}" != "${curd}"
		   echo 71.ok
		else
		   echo 71.err
		endif
		if $diet == ${curd}
		   echo 72.err
		else
		   echo 72.ok
		endif
		if ${diet} == $curd
		   echo 73.err
		else
		   echo 73.ok
		endif
		# Unary !
		if ! 0 && ! ! 1 && ! ! ! ! 2 && 3
		   echo 80.ok
		else
		   echo 80.err
		endif
		if ! 0 && ! [ ! 1 ] && ! [ ! [ ! [ ! 2 ] ] ] && 3
		   echo 81.ok
		else
		   echo 81.err
		endif
		if [ ! 0 ] && [ ! [ ! 1 ] ] && [ ! [ ! [ ! [ ! [ 2 ] ] ] ] ] && 3
		   echo 82.ok
		else
		   echo 82.err
		endif
		if [ ! 0 ] && [ ! [ ! 1 ] ] && [ ! [ ! [ ! [ ! [ 2 ] ] ] ] ] && ! 3
		   echo 83.err
		else
		   echo 83.ok
		endif
		if [ ! 0 ] && [ ! [ ! 1 ] ] && ! [ [ ! [ ! [ ! [ 2 ] ] ] ] ] && ! 3
		   echo 84.err
		else
		   echo 84.ok
		endif
		if [ ! 0 ] && ! [ ! [ ! 1 ] ] && [ ! [ ! [ ! [ ! [ 2 ] ] ] ] ] && 3
		   echo 85.err
		else
		   echo 85.ok
		endif
		if ! [ ! 0 ] && [ ! [ ! 1 ] ] && [ ! [ ! [ ! [ ! [ 2 ] ] ] ] ] && 3
		   echo 86.err
		else
		   echo 86.ok
		endif
		if [ ! 0 ] && [ ! [ ! 1 ] ] && [ ! [ ! [ ! [ ! [ 2 ] ] ] ] ] || 3
		   echo 87.ok
		else
		   echo 87.err
		endif
		if [ ! 0 ] && [ ! ! [ ! ! 1 ] ] && [ ! ! [ ! ! [ ! ! [ ! ! [ 2 ] ] ] ] ]
		   echo 88.ok
		else
		   echo 88.err
		endif
		# Unary !, odd
		if ! 0 && ! ! 1 && ! ! ! 0 && 3
		   echo 90.ok
		else
		   echo 90.err
		endif
		if ! 0 && ! [ ! 1 ] && ! [ ! [ ! [ 0 ] ] ] && 3
		   echo 91.ok
		else
		   echo 91.err
		endif
		if [ ! 0 ] && [ ! [ ! 1 ] ] && [ ! [ ! [ ! [ [ 0 ] ] ] ] ] && 3
		   echo 92.ok
		else
		   echo 92.err
		endif
		if [ ! 0 ] && [ ! [ ! 1 ] ] && [ ! [ ! ! [ ! [ ! 0 ] ] ] ] && ! 3
		   echo 93.err
		else
		   echo 93.ok
		endif
		if [ ! 0 ] && [ ! [ ! 1 ] ] && ! [ ! [ ! [ ! [ ! 0 ] ] ] ] && 3
		   echo 94.ok
		else
		   echo 94.err
		endif
		if [ ! 0 ] && ! [ ! [ ! 1 ] ] && [ ! ! [ ! [ ! [ ! [ 0 ] ] ] ] ] && 3
		   echo 95.err
		else
		   echo 95.ok
		endif
		if ! [ ! 0 ] && [ ! [ ! 1 ] ] && [ ! [ ! [ ! [ ! ! 0 ] ] ] ] && 3
		   echo 96.err
		else
		   echo 96.ok
		endif
		if [ ! 0 ] && [ ! [ ! 1 ] ] && [ ! [ ! [ ! [ ! [ ! 0 ] ] ] ] ] || 3
		   echo 97.ok
		else
		   echo 97.err
		endif
		if [ ! 0 ] && [ ! ! [ ! ! 1 ] ] && [ ! ! [ ! ! [ ! ! [ ! [ 0 ] ] ] ] ]
		   echo 98.ok
		else
		   echo 98.err
		endif
	__EOT
   #}}}
   check normal 0 ./tnormal '1688759742 719'

   if have_feat regex; then
      #{{{
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tregex
			set dietcurd=yoho
			if $dietcurd =~ '^yo.*'
			   echo 1.ok
			else
			   echo 1.err
			endif
			if "$dietcurd" =~ '^Yo.*'
			   echo 1-1.err
			else
			   echo 1-1.ok
			endif
			if $dietcurd =~?case '^Yo.*'
			   echo 1-2.ok
			else
			   echo 1-2.err
			endif
			if $dietcurd =~ '^yOho.+'
			   echo 2.err
			else
			   echo 2.ok
			endif
			if $dietcurd !~? '.*Ho$'
			   echo 3.err
			else
			   echo 3.ok
			endif
			if $dietcurd !~ '.+yohO$'
			   echo 4.ok
			else
			   echo 4.err
			endif
			if [ $dietcurd !~?cas '.+yoho$' ]
			   echo 5.ok
			else
			   echo 5.err
			endif
			if ! [ "$dietcurd" =~?case '.+yoho$' ]
			   echo 6.ok
			else
			   echo 6.err
			endif
			if ! ! [ $'\$dietcurd' !~? '.+yoho$' ]
			   echo 7.ok
			else
			   echo 7.err
			endif
			if ! [ ! [ $dietcurd !~? '.+yoho$' ] ]
			   echo 8.ok
			else
			   echo 8.err
			endif
			if [ ! [ ! [ $dietcurd !~? '.+yoho$' ] ] ]
			   echo 9.ok
			else
			   echo 9.err
			endif
			if ! [ ! [ ! [ $dietcurd !~ '.+yoho$' ] ] ]
			   echo 10.err
			else
			   echo 10.ok
			endif
			if !  ! ! $dietcurd !~ '.+yoho$'
			   echo 11.err
			else
			   echo 11.ok
			endif
			if !  ! ! $dietcurd =~ '.+yoho$'
			   echo 12.ok
			else
			   echo 12.err
			endif
			if ! [ ! ! [ ! [ $dietcurd !~ '.+yoho$' ] ] ]
			   echo 13.ok
			else
			   echo 13.err
			endif
			set diet=abc curd='^abc$'
			if $diet =~ $curd
			   echo 14.ok
			else
			   echo 14.err
			endif
			set diet=abc curd='^abcd$'
			if "$diet" !~ $'\$curd'
			   echo 15.ok
			else
			   echo 15.err
			endif
		__EOT
      #}}}
      check regex 0 ./tregex '1115671789 95'

      #{{{
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tregex-match
      commandalias x \\echo '$?/$^ERRNAME.. $^#<$^0, $^1, $^2, $^3, $^4>'
      commandalias e \\echoerr err:
      \if abrakadabra =~ (.+)ka.*; x; \else; e 1; \end
      \if abrakadabra =~ ^.*(ra)(.*)(ra)'$'; x; \else; e 2; \end
      \if abrakadabra !~ (.+)no.*; x; \else; e 3; \end
      \if bananarama =~?case (.*)NANA(.*); x; \else; e 4; \end
		__EOT
      #}}}
      check regex-match 0 ./tregex-match '1787920457 135'
   else
      t_echoskip 'regex,regex-match:[!REGEX]'
   fi

   t_epilog "${@}"
} # }}}

t_localopts() { # {{{
   t_prolog "${@}"

   #{{{ Nestable conditions test
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
	define t2 {
	   echo in: t2
	   set t2=t2
	   echo $t2
	}
	define t1 {
	   echo in: t1
	   set gv1=gv1
	   localopts on
	   set lv1=lv1 lv2=lv2
	   set lv3=lv3
	   call t2
	   localopts off
	   set gv2=gv2
	   echo $gv1 $lv1 ${lv2} ${lv3} ${gv2}, $t2
	}
	define t0 {
	   echo in: t0
	   call t1
	   echo $gv1 $lv1 ${lv2} ${lv3} ${gv2}, $t2
	   echo "$gv1 $lv1 ${lv2} ${lv3} ${gv2}, $t2"
	}
	account trouble {
	   echo in: trouble
	   call t0
	}
	call t0
	unset gv1 gv2
	account trouble
	echo active trouble: $gv1 $lv1 ${lv2} ${lv3} ${gv2}, $t3
	account null
	echo active null: $gv1 $lv1 ${lv2} ${lv3} ${gv2}, $t3

   #
   define ll2 {
      localopts $1
      set x=2
      echo ll2=$x
   }
   define ll1 {
      set y=$1; shift; eval localopts $y; localopts $1; shift
      set x=1
      echo ll1.1=$x
      call ll2 $1
      echo ll1.2=$x
   }
   define ll0 {
      set y=$1; shift; eval localopts $y; localopts $1; shift
      set x=0
      echo ll0.1=$x
      call ll1 $y "$@"
      echo ll0.2=$x
   }
   define llx {
      echo ----- $1: $2 -> $3 -> $4
      echo ll-1.1=$x
      eval localopts $1
      call ll0 "$@"
      echo ll-1.2=$x
      unset x
   }
   define lly {
      call llx 'call off' on on on
      call llx 'call off' off on on
      call llx 'call off' on off on
      call llx 'call off' on off off
      localopts call-fixate on
      call llx 'call-fixate on' on on on
      call llx 'call-fixate on' off on on
      call llx 'call-fixate on' on off on
      call llx 'call-fixate on' on off off
      unset x;localopts call on
      call llx 'call on' on on on
      call llx 'call on' off on on
      call llx 'call on' on off on
      call llx 'call on' on off off
   }
   call lly
	__EOT
   #}}}
   check 1 0 ./t1 '4016155249 1246'

   t_epilog "${@}"
} # }}}

t_local() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
		define du2 {
			echo du2-1 du=$du
			local set du=$1
			echo du2-2 du=$du
			local unset du
			echon du2-3\ <$du>\ ; varshow du
			local set du=$1
			echo du2-4 du=$du
			local set nodu
			echon du2-5\ <$du>\ ; varshow du
		}
		define du {
			local set du=dudu
			echo du-1 du=$du
			call du2 du2du2
			echo du-2 du=$du
		}
		define ich {
			echo ich-1 du=$du
			call du
			echo ich-2 du=$du
		}
		define wir {
			set du=wirwir
			echo wir-1 du=$du
			eval $1 call ich
			echo wir-2 du=$du
		}
		echo ------- global-1 du=$du
		call ich
		echo ------- global-2 du=$du
		set du=global
		call ich
		echo ------- global-3 du=$du
		local call wir local
		echo ------- global-4 du=$du
		call wir
		echo ------- global-5 du=$du
		__EOT
   #}}}
   check 1 0 ./t1 '291008203 807'

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t2 2>&1
		define z {
			echo z-1: x=$x y=$y z=$z crt=$crt
			local set z=1 y=2 crt=10
			echo z-2: x=$x y=$y z=$z crt=$crt
		}
		define y {
			echo y-1: x=$x y=$y z=$z crt=$crt
			local set x=2 y=1 crt=5
			echo y-2: x=$x y=$y z=$z crt=$crt
			call z
			echo y-3: x=$x y=$y z=$z crt=$crt
		}
		define x {
			echo x-1: x=$x y=$y z=$z crt=$crt
			local set x=1 crt=1
			echo x-2: x=$x y=$y z=$z crt=$crt
			call y
			echo x-3: x=$x y=$y z=$z crt=$crt
		}
		set crt
		echo global-1: x=$x y=$y z=$z crt=$crt
		call x
		echo global-2: x=$x y=$y z=$z crt=$crt
		__EOT
   #}}}
   check 2 0 ./t2 '2560788669 216'

   t_epilog "${@}"
} # }}}

t_environ() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | EK1=EV1 EK2=EV2 ${MAILX} ${ARGS} > ./t1 2>&1
	echo "we: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	!echo "shell: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	varshow EK1 EK2 EK3 EK4 NEK5

	echo environ set EK3 EK4, set NEK5
	environ set EK3=EV3 EK4=EV4
	set NEK5=NEV5
	echo "we: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	!echo "shell: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	varshow EK1 EK2 EK3 EK4 NEK5

	echo removing NEK5 EK3
	unset NEK5
	environ unset EK3
	echo "we: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	!echo "shell: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	varshow EK1 EK2 EK3 EK4 NEK5

	echo changing EK1, EK4
	set EK1=EV1_CHANGED EK4=EV4_CHANGED
	echo "we: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	!echo "shell: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	varshow EK1 EK2 EK3 EK4 NEK5

	echo linking EK4, rechanging EK1, EK4
	environ link EK4
	set EK1=EV1 EK4=EV4
	echo "we: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	!echo "shell: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	varshow EK1 EK2 EK3 EK4 NEK5

	echo unset all
	unset EK1 EK2 EK4
	echo "we: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	!echo "shell: EK1<$EK1> EK2<$EK2> EK3<$EK3> EK4<$EK4> NEK5<$NEK5>"
	varshow EK1 EK2 EK3 EK4 NEK5
	__EOT
   #}}}
   check 1 0 ./t1 '2826722558 1100'

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t2 2>&1
	define l4 {
		echo --l4-in;show
		eval $1 environ unlink LK1
		eval $1 environ unset EK1
		local set LK1=LK1_L4
		echo --l4-ou;show
	}
	define l3 {
		echo --l3-in;show
		set LK1=LK1_L3 EK1=EK1_L3
		echo --l3-mid;show
		call l4 local
		echo --l3-preou;show
		local xcall l4
	}
	define l3.local {
		echo --l3.local-in;show
		local set LK1=LK1_L3.local
		echo --l3.local-ou;show
	}
	define l2 {
		echo --l2-in;show
		set LK1=LK1_L2 EK1=EK1_L2
		echo --l2-mid-1;show
		call l3
		echo --l2-mid-2;show
		call l3.local
		echo --l2-ou;show
	}
	define l1 {
		echo --l1-in;show
		environ set LK1=LK1_L1 EK1=EK1_L1
		environ link LK1
		echo --l1-mid;show
		local call l2
		echo --l1-ou;show
	}
	commandalias show \
		'echo LK1=$LK1 EK1=$EK1;\
		varshow LK1 EK1;\
		!echo shell" LK1<$LK1> EK1<$EK1>"'
		environ set EK1=EV1 noLK1
		environ link LK1
	echo --toplevel-in; show
	call l1
	echo --toplevel-ou; show
	__EOT
   #}}}
   check_ex0 2-estat
   have_feat uistrings && ck='1900155792 1889' || ck='2028138132 1821'
   check 2 - ./t2 "${ck}"

   # Rather redundant, but came up during tests so let's use it
   if have_feat cmd-vexpr; then
      #{{{
      ${cat} <<- '__EOT' > ./t3_4_5.dat
	set recu=0
	define du {
		echon 1 only env (outer) du=$du:; var du; !echo sh=$du
		local set du=au
		echon 2 only local (au) du=$du:; var du; !echo sh=$du
		environ link du
		echon 3 also env linked du=$du:; var du; !echo sh=$du
		vput vexpr recu + $recu 1
		if $recu -eq 1
			echo ----------------RECURSION STARTS
			call du
			echon ----------------RECURSION ENDS: du=$du:; var du; !echo sh=$du
		end
		local unset du
		echon 4 local ($recu) unset du=$du:; var du; !echo sh=$du
		vput vexpr recu + $recu 1
		set du=updated$recu
		echon 5 updated ($recu) du=$du:; var du; !echo sh=$du
	}
	echon outer-1 du=$du:; var du; !echo sh=$du
	call du
	echon outer-2 du=$du:; var du; !echo sh=$du
		__EOT
      #}}}
      < ./t3_4_5.dat du=outer ${MAILX} ${ARGS} > ./t3 2>&1
      check 3 0 ./t3 '3778746136 711'

      unset du
      < ./t3_4_5.dat ${MAILX} ${ARGS} > ./t4 2>&1
      check_ex0 4-estat
      have_feat uistrings && ck='1967950703 693' || ck='1243820518 559'
      check 4 - ./t4 "${ck}"

      < ./t3_4_5.dat ${MAILX} ${ARGS} -Y 'set du=via-y' > ./t5 2>&1
      check 5 0 ./t5 '3094640490 700'
   else
      t_echoskip '3-5:[!CMD_VEXPR]'
   fi

   #{{{ lookup
   unset du
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t6 2>&1
	commandalias x echon '$?/$^ERRNAME\; du=$du\; d1=$d1:;var du'
	environ lookup du
	x
	vput environ d1 lookup du
	x
	environ set du=1
	environ lookup du
	x
	vput environ d1 lookup du
	x
	vput environ d+ lookup du
	x
	__EOT
   #}}}
   check_ex0 6-estat
   have_feat uistrings && ck='2303278002 242' || ck='3150923704 217'
   check 6 - ./t6 "${ck}"

   t_epilog "${@}"
} # }}}

t_loptlocenv() { # 1 combined 4 :) {{{
   t_prolog "${@}"

   if have_feat cmd-vexpr; then :; else
      t_echoskip '[!CMD_VEXPR]'
      t_epilog "${@}"
      return
   fi

   #{{{
   ${cat} <<- '__EOT' > t.rc
	set _x=x
	# We torture the unroll stack a bit more with that
	if "$features" =% +cmd-vexpr,
		set _y
	endif
	define r5 {
		# Break a link, too
		local environ unlink zz; local set zz=BLA
		echon 'ln-broken: '; varshow zz; !echo ln-broken, shell" zz=$zz"
	}
	define r4 {
		if $# -eq 1
			echo --r4-${1}-in;show
			call r5
			echon 'ln-restored: '; varshow zz; !echo ln-restored, shell" zz=$zz"
		endif
		local environ unset zz
		local set noasksub notoplines noxy yz=YZ! zz=!ZY no_S_MAILX_TEST
		if -Z _y || [ $# -eq 2 && "$2" -ge 50 ]
			echo --r4-${1}-ou;show
			return
		endif
		local vput vexpr i + "$2" 1
		eval ${_x}call r4 $1 $i
	}
	define r3 {
		echo --r3-in;show
		local set asksub toplines=21 xy=huhu yz=vu zz=uv _S_MAILX_TEST=wadde
		echo --r3-mid;show
		call r4 1
		set _x=
		echo --r3-mid-2;show
		call r4 2
		echo --r3-ou;show
	}
	define r2 {
		echo --r2-in;show
		local unset asksub toplines xy yz _S_MAILX_TEST
		echo --r2-prexcall;show
		xcall r3
	}
	define r1 {
		echo --r1-in;show
		set xy=bye
		local set asksub=du toplines=10 _S_MAILX_TEST=gelle
		local environ set yz=cry zz=yrc
		echo --r1-mid;show
		local call r2
		echo --r1-ou;show
	}
	define r0 {
		echo --r0-in;show
		unset asksub
		echo --r0-mi;show
		local call r1
		echo --r0-ou;show
	}
	commandalias show \
		'echo as=$asksub tl=$toplines xy=$xy yz=$yz zz=$zz MT=$_S_MAILX_TEST;\
		varshow asksub toplines xy yz zz _S_MAILX_TEST;\
		!echo shell" a=$ask tl=$toplines xy=$xy yz=$yz zz=$zz MT=$_S_MAILX_TEST"'
	echo 'test asserts asksub tl=5 noxy noyz no_S_MAILX_TEST'
	varshow asksub toplines xy yz _S_MAILX_TEST
	if $? -ne 0; echo Error; else; echo OK; endif
	set asksub toplines=4 xy=hi _S_MAILX_TEST=trabbel
	environ set yz=fi
	environ link zz
	echo --toplevel-in; show
	call r0
	echo --toplevel-ou; show
	__EOT
   #}}}
   </dev/null MAILRC=./t.rc ask= zz=if ${MAILX} ${ARGS} -:u \
      -X echo\ --Xcommline\;show > ./t1 2>&1
   check 1 0 ./t1 '2212856329 3957'

   </dev/null MAILRC=./t.rc ask= zz=if ${MAILX} ${ARGS} -:u \
      -X echo\ --Xcommline\;show \
      -S noasksub -S toplines=7 \
      > ./t2 2>&1
   check 2 0 ./t2 '1797343641 4007'

   t_epilog "${@}"
} # }}}

t_macro_param_shift() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>>${ERR}
	define t2 {
	   echo in: t2
	   echo t2.0 has $#/${#} parameters: "$1,${2},$3" (${*}) [$@]
	   local set ignerr=$1
	   shift
	   echo t2.1 has $#/${#} parameters: "$1,${2},$3" (${*}) [$@]
	   if [ $# > 1 ] || [ $ignerr == '' ]
	      shift 2
	   else
	      ignerr shift 2
	   endif
	   echo t2.2:$? has $#/${#} parameters: "$1,${2},$3" (${*}) [$@]
	   shift 0
	   echo t2.3:$? has $#/${#} parameters: "$1,${2},$3" (${*}) [$@]
	   if [ $# > 0 ]
	      shift
	   endif
	   echo t2.4:$? has $#/${#} parameters: "$1,${2},$3" (${*}) [$@]
	}
	define t1 {
	   set errexit
	   echo in: t1
	   call t2 1 you get four args
	   echo t1.1: $?';' ignerr ($ignerr) should not exist
	   call t2 1 you get 'three args'
	   echo t1.2: $?';' ignerr ($ignerr) should not exist
	   call t2 1 you 'get two args'
	   echo t1.3: $?';' ignerr ($ignerr) should not exist
	   call t2 1 'you get one arg'
	   echo t1.4: $?';' ignerr ($ignerr) should not exist
	   ignerr call t2 '' 'you get one arg'
	   echo t1.5: $?';' ignerr ($ignerr) should not exist
	}
	call t1
	__EOT
   #}}}
   check 1 0 ./t1 '1402489146 1682'

   t_epilog "${@}"
} # }}}

t_csop() { # {{{
   t_prolog "${@}"

   if have_feat cmd-csop; then :; else
      t_echoskip '[!CMD_CSOP]'
      t_epilog "${@}"
      return
   fi

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
	commandalias x echo '$?/$^ERRNAME :$res:'
	echo ' #-2'
	vput csop res find you y;x
	vput csop res find you o;x
	vput csop res find you u;x
	vput csop res find you yo;x
	vput csop res find you ou;x
	vput csop res find you you;x
	echo ' #-1'
	vput csop res find you Y;x
	vput csop res find? you Y;x
	vput csop res find?case you O;x
	vput csop res find? you U;x
	vput csop res find?ca you yO;x
	vput csop res find? you oU;x
	vput csop res find? you YoU;x
	echo ' #0'
	vput csop res find 'bananarama' 'nana';x
	vput csop res find 'bananarama' 'bana';x
	vput csop res find 'bananarama' 'Bana';x
	vput csop res find 'bananarama' 'rama';x
	echo ' #1'
	vput csop res find? 'bananarama' 'nana';x
	vput csop res find? 'bananarama' 'bana';x
	vput csop res find? 'bananarama' 'Bana';x
	vput csop res find? 'bananarama' 'rama';x
	echo ' #2'
	vput csop res substring 'bananarama' 1;x
	vput csop res substring 'bananarama' 3;x
	vput csop res substring 'bananarama' 5;x
	vput csop res substring 'bananarama' 7;x
	vput csop res substring 'bananarama' 9;x
	vput csop res substring 'bananarama' 10;x
	vput csop res substring 'bananarama' 1 3;x
	vput csop res substring 'bananarama' 3 3;x
	vput csop res substring 'bananarama' 5 3;x
	vput csop res substring 'bananarama' 7 3;x
	vput csop res substring 'bananarama' 9 3;x
	vput csop res substring 'bananarama' 10 3;x
	echo ' #3'
	vput csop res substring 'bananarama' -1;x
	vput csop res substring 'bananarama' -3;x
	vput csop res substring 'bananarama' -5;x
	vput csop res substring 'bananarama' -7;x
	vput csop res substring 'bananarama' -9;x
	vput csop res substring 'bananarama' -10;x
	vput csop res substring 'bananarama' 1 -3;x
	vput csop res substring 'bananarama' 3 -3;x
	vput csop res substring 'bananarama' 5 -3;x
	vput csop res substring 'bananarama' 7 -3;x
	vput csop res substring 'bananarama' 9 -3;x
	vput csop res substring 'bananarama' 10 -3;x
	echo ' #4'
	vput csop res trim 'Cocoon  Cocoon';x
	vput csop res trim '  Cocoon  Cocoon 	  ';x
	vput csop res trim-front 'Cocoon  Cocoon';x
	vput csop res trim-front '  Cocoon  Cocoon 	  ';x
	vput csop res trim-end 'Cocoon  Cocoon';x
	vput csop res trim-end '  Cocoon  Cocoon 	  ';x
	__EOT
   #}}}
   check 1 0 ./t1 '1892119538 755'

   t_epilog "${@}"
} # }}}

t_vexpr() { # {{{
   t_prolog "${@}"

   if have_feat cmd-vexpr; then :; else
      t_echoskip '[!CMD_VEXPR]'
      t_epilog "${@}"
      return
   fi

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tnumeric 2>>${ERR}
	commandalias x echo '$?/$^ERRNAME $res'
	echo ' #0.0'
	vput vexpr res = 9223372036854775807;x
	vput vexpr res = 9223372036854775808;x
	vput vexpr res = u9223372036854775808;x
	vput vexpr res =? 9223372036854775808;x
	vput vexpr res = -9223372036854775808;x
	vput vexpr res = -9223372036854775809;x
	vput vexpr res =?saturated -9223372036854775809;x
	vput vexpr res = U9223372036854775809;x
	vput vexpr res = 64#7__________;x
	vput vexpr res = 64#7@@@@@@@@@@;x
	echo ' #0.1'
	vput vexpr res = \
		 0b0111111111111111111111111111111111111111111111111111111111111111;x
	vput vexpr res = \
		s0b1000000000000000000000000000000000000000000000000000000000000000;x
	vput vexpr res =? \
		S0b10000000000000000000000000000000000000000000000000000000000000000;x
	vput vexpr res = \
		U0b1000000000000000000000000000000000000000000000000000000000000000;x
	vput vexpr res = \
		 0b1000000000000000000000000000000000000000000000000000000000000000;x
	vput vexpr res =? \
		 0b1000000000000000000000000000000000000000000000000000000000000000;x
	vput vexpr res = \
		-0b1000000000000000000000000000000000000000000000000000000000000000;x
	vput vexpr res = \
		S0b1000000000000000000000000000000000000000000000000000000000000001;x
	vput vexpr res =? \
		S0b1000000000000000000000000000000000000000000000000000000000000001;x
	vput vexpr res =? \
		-0b1000000000000000000000000000000000000000000000000000000000000001;x
	vput vexpr res = \
		U0b1000000000000000000000000000000000000000000000000000000000000001;x
	echo ' #0.2'
	vput vexpr res = 0777777777777777777777;x
	vput vexpr res = S01000000000000000000000;x
	vput vexpr res =? S01000000000000000000000;x
	vput vexpr res = U01000000000000000000000;x
	vput vexpr res = 01000000000000000000000;x
	vput vexpr res =?satur 01000000000000000000000;x
	vput vexpr res = -01000000000000000000000;x
	vput vexpr res = S01000000000000000000001;x
	vput vexpr res =?sat S01000000000000000000001;x
	vput vexpr res = -01000000000000000000001;x
	vput vexpr res = U01000000000000000000001;x
	echo ' #0.3'
	vput vexpr res = 0x7FFFFFFFFFFFFFFF;x
	vput vexpr res = S0x8000000000000000;x
	vput vexpr res =? S0x8000000000000000;x
	vput vexpr res = U0x8000000000000000;x
	vput vexpr res = 0x8000000000000000;x
	vput vexpr res =? 0x8000000000000000;x
	vput vexpr res = -0x8000000000000000;x
	vput vexpr res = S0x8000000000000001;x
	vput vexpr res =? S0x8000000000000001;x
	vput vexpr res = -0x8000000000000001;x
	vput vexpr res = u0x8000000000000001;x
	vput vexpr res = 9223372036854775809;x
	vput vexpr res =? 9223372036854775809;x
	vput vexpr res = u9223372036854775809;x
	echo ' #1'
	vput vexpr res ~ 0;x
	vput vexpr res ~ 1;x
	vput vexpr res ~ -1;x
	echo ' #1.1'
	vput vexpr res - 0;x
	vput vexpr res - 1;x
	vput vexpr res - -1;x
	vput vexpr res - -0xAFFE;x
	vput vexpr res - 0xAFFE;x
	vput vexpr res - u0x8000000000000001;x
	vput vexpr res - 0x8000000000000001;x
	vput vexpr res - 0x8000000000000001;x
	vput vexpr res - 9223372036854775809;x
	vput vexpr res -? 9223372036854775809;x
	echo ' #1.2'
	vput vexpr res + 0;x
	vput vexpr res + 1;x
	vput vexpr res + -1;x
	vput vexpr res + -0xAFFE;x
	vput vexpr res + 0xAFFE;x
	vput vexpr res + u0x8000000000000001;x
	vput vexpr res + 0x8000000000000001;x
	vput vexpr res + 9223372036854775809;x
	vput vexpr res +? 9223372036854775809;x
	echo ' #2'
	vput vexpr res + 0 0;x
	vput vexpr res + 0 1;x
	vput vexpr res + 1 1;x
	echo ' #3'
	vput vexpr res + 9223372036854775807 0;x
	vput vexpr res + 9223372036854775807 1;x
	vput vexpr res +? 9223372036854775807 1;x
	vput vexpr res + 0 9223372036854775807;x
	vput vexpr res + 1 9223372036854775807;x
	vput vexpr res +? 1 9223372036854775807;x
	echo ' #4'
	vput vexpr res + -9223372036854775808 0;x
	vput vexpr res + -9223372036854775808 -1;x
	vput vexpr res +? -9223372036854775808 -1;x
	vput vexpr res + 0 -9223372036854775808;x
	vput vexpr res + -1 -9223372036854775808;x
	vput vexpr res +? -1 -9223372036854775808;x
	echo ' #5'
	vput vexpr res - 0 0;x
	vput vexpr res - 0 1;x
	vput vexpr res - 1 1;x
	echo ' #6'
	vput vexpr res - 9223372036854775807 0;x
	vput vexpr res - 9223372036854775807 -1;x
	vput vexpr res -? 9223372036854775807 -1;x
	vput vexpr res - 0 9223372036854775807;x
	vput vexpr res - -1 9223372036854775807;x
	vput vexpr res - -2 9223372036854775807;x
	vput vexpr res -? -2 9223372036854775807;x
	echo ' #7'
	vput vexpr res - -9223372036854775808 +0;x
	vput vexpr res - -9223372036854775808 +1;x
	vput vexpr res -? -9223372036854775808 +1;x
	vput vexpr res - 0 -9223372036854775808;x
	vput vexpr res - +1 -9223372036854775808;x
	vput vexpr res -? +1 -9223372036854775808;x
	echo ' #8'
	vput vexpr res + -13 -2;x
	vput vexpr res - 0 0;x
	vput vexpr res - 0 1;x
	vput vexpr res - 1 1;x
	vput vexpr res - -13 -2;x
	echo ' #9'
	vput vexpr res * 0 0;x
	vput vexpr res * 0 1;x
	vput vexpr res * 1 1;x
	vput vexpr res * -13 -2;x
	echo ' #10'
	vput vexpr res / 0 0;x
	vput vexpr res / 0 1;x
	vput vexpr res / 1 1;x
	vput vexpr res / -13 -2;x
	echo ' #11'
	vput vexpr res % 0 0;x
	vput vexpr res % 0 1;x
	vput vexpr res % 1 1;x
	vput vexpr res % -13 -2;x
	echo ' #12'
	vput vexpr res pbase 10 u0x8000000000000001;x
	vput vexpr res pbase 16 0x8000000000000001;x
	vput vexpr res pbase 16 s0x8000000000000001;x
	vput vexpr res pbase 16 u0x8000000000000001;x
	vput vexpr res pbase 36 0x8000000000000001;x
	vput vexpr res pbase 36 u0x8000000000000001;x
	vput vexpr res pbase 64 0xF64D7EFE7CBD;x
	vput vexpr res pbase 64 0776767676767676767676;x
	echo ' #13'
	vput vexpr res << 0 1;x
	vput vexpr res << 1 1;x
	vput vexpr res << 0x80 55;x
	vput vexpr res << 0x80 56;x
	vput vexpr res >>> 0xE000000000000000 56;x
	vput vexpr res >>> 0x8000000000000000 56;x
	vput vexpr res >>> 0x7F00000000000000 56;x
	vput vexpr res >> 0xE000000000000000 56;x
	vput vexpr res >> 0x8000000000000000 56;x
	vput vexpr res >> 0x7F00000000000000 56;x
	vput vexpr res << 1 -1;x
	__EOT
   #}}}
   check numeric 0 ./tnumeric '3600543675 2773'

   if have_feat regex; then
      #{{{
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tregex 2>&1
		commandalias x echo '$?/$^ERRNAME :$res:'
		echo ' #-2'
		vput vexpr res regex you y;x
		vput vexpr res regex you o;x
		vput vexpr res regex you u;x
		vput vexpr res regex you yo;x
		vput vexpr res regex you ou;x
		vput vexpr res regex you you;x
		echo ' #-1'
		vput vexpr res regex you Y;x
		vput vexpr res regex? you Y;x
		vput vexpr res regex? you O;x
		vput vexpr res regex? you U;x
		vput vexpr res regex? you yO;x
		vput vexpr res regex? you oU;x
		vput vexpr res regex? you YoU;x
		echo ' #0'
		vput vexpr res regex 'bananarama' 'nana';x
		vput vexpr res regex 'bananarama' 'bana';x
		vput vexpr res regex 'bananarama' 'Bana';x
		vput vexpr res regex 'bananarama' 'rama';x
		echo ' #1'
		vput vexpr res regex? 'bananarama' 'nana';x
		vput vexpr res regex? 'bananarama' 'bana';x
		vput vexpr res regex? 'bananarama' 'Bana';x
		vput vexpr res regex? 'bananarama' 'rama';x
		echo ' #2'
		vput vexpr res regex 'bananarama' '(.*)nana(.*)' '\${1}a\${0}u{\$2}';x
		vput vexpr res regex 'bananarama' '(.*)bana(.*)' '\${1}a\${0}u\$2';x
		vput vexpr res regex 'bananarama' 'Bana(.+)' '\$1\$0';x
		vput vexpr res regex 'bananarama' '(.+)rama' '\$1\$0';x
		echo ' #3'
		vput vexpr res regex? 'bananarama' '(.*)nana(.*)' '\${1}a\${0}u{\$2}';x
		vput vexpr res regex? 'bananarama' '(.*)bana(.*)' '\${1}a\${0}u\$2';x
		vput vexpr res regex? 'bananarama' 'Bana(.+)' '\$1\$0';x
		vput vexpr res regex? 'bananarama' '(.+)rama' '\$1\$0';x
		echo ' #4'
		vput vexpr res regex 'banana' '(club )?(.*)(nana)(.*)' \
			'\$1\${2}\$4\${3}rama';x
		vput vexpr res regex 'Banana' '(club )?(.*)(nana)(.*)' \
			'\$1\$2\${2}\$2\$4\${3}rama';x
		vput vexpr res regex 'Club banana' '(club )?(.*)(nana)(.*)' \
			'\$1\${2}\$4\${3}rama';x
		vput vexpr res regex 'Club banana' '(club )?(.*)(nana)(.*)' \
			'\$1:\${2}:\$4:\${3}';x
		vput vexpr res regex? 'Club banana' '(club )?(.*)(nana)(.*)' \
			'\$1:\${2}:\$4:\${3}';x
		echo ' #5'
		__EOT
      #}}}
      check regex 0 ./tregex '1617405672 590'
   else
      t_echoskip 'regex:[!REGEX]'
   fi

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tagnostic 2>&1
	commandalias x echo '$?/$^ERRNAME :$res:'
	vput vexpr res date-utc 1620942446;x
	eval set $res
	if 2021-5-13T21:47:26:May != "${dutc_year}-${dutc_month}-${dutc_day}T"\
		"$dutc_hour:$dutc_min:$dutc_sec:$dutc_month_abbrev"; echo ERROR; endif
	vput vexpr res epoch 2021 05 13 21 47 26;x
	eval set $res
	if 16209424460 != "$epoch_sec$epoch_nsec"; echo ERROR; endif
	#
	vput vexpr res date-utc 0x1D30BE2E1FF;x
	eval set $res
	if 65535-12-31T23:59:59:Dec != "${dutc_year}-${dutc_month}-${dutc_day}T"\
		"$dutc_hour:$dutc_min:$dutc_sec:$dutc_month_abbrev"; echo ERROR; endif
	vput vexpr res epoch 65535 12 31 23 59 59;x
	eval set $res
	if 20059491455990 != "$epoch_sec$epoch_nsec"; echo ERROR; endif
	#
	vput vexpr res date-utc 951786123;x
	eval set $res
	if 2000-2-29T1:2:3 != "${dutc_year}-${dutc_month}-${dutc_day}T"\
		"$dutc_hour:$dutc_min:$dutc_sec"; echo ERROR; endif
	vput vexpr res epoch 2000 02 29 01 02 03;x
	eval set $res
	if 9517861230 != "$epoch_sec$epoch_nsec"; echo ERROR; endif
	#
	vput vexpr res date-utc 1582938123;x
	eval set $res
	if 2020-2-29T1:2:3 != "${dutc_year}-${dutc_month}-${dutc_day}T"\
		"$dutc_hour:$dutc_min:$dutc_sec"; echo ERROR; endif
	vput vexpr res epoch 2020 02 29 01 02 03;x
	eval set $res
	if 15829381230 != "$epoch_sec$epoch_nsec"; echo ERROR; endif
	__EOT
   #}}}
   check agnostic 0 ./tagnostic '638877393 602'

   t_epilog "${@}"
} # }}}

t_call_ret() { # {{{
   t_prolog "${@}"

   if have_feat cmd-vexpr; then :; else
      t_echoskip '[!CMD_VEXPR]'
      t_epilog "${@}"
      return
   fi

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -Snomemdebug > ./t1 2>&1
	define w1 {
		echon ">$1 "
		local vput vexpr i + $1 1
		if $i -le 42
			local vput vexpr j & $i 7
			if $j -eq 7; echo .; end
			call w1 $i
			set i=$? k=$!
			local vput vexpr j & $i 7
			echon "<$1/$i/$k "
			if $j -eq 7; echo .; end
		else
			echo ! The end for $1
		end
		return $1
	}
	# Transport $?/$! up the call chain
	define w2 {
		echon ">$1 "
		local vput vexpr i + $1 1
		if $1 -lt 42
			call w2 $i
			local set i=$? j=$! k=$^ERRNAME
			echon "<$1/$i/$k "
			return $i $j
		else
			echo ! The end for $1
			return $i $^ERR-BUSY
		end
		echoerr au
	}
	# Up and down it goes
	define w3 {
		echon ">$1/$2 "
		local vput vexpr i + $1 1
		if $1 -lt 42
			call w3 $i $2
			local set i=$? j=$!
			local vput vexpr k - $1 $2
			if $k -eq 21
				vput vexpr i + $1 1
				vput vexpr j + $2 1
				echo "# <$i/$j> .. "
				call w3 $i $j
				set i=$? j=$!
			end
			eval echon "<\$1=\$i/\$^ERRNAME-$j "
			return $i $j
		else
			echo ! The end for $1=$i/$2
         if "$2" != ""
            return $i $^ERR-DOM
         else
            return $i $^ERR-BUSY
         end
		end
		echoerr au
	}

	call w1 0; echo ?=$? !=$!; echo -----;
	call w2 0; echo ?=$? !=$^ERRNAME; echo -----;
	call w3 0 1; echo ?=$? !=$^ERRNAME; echo -----;
	__EOT
   #}}}
   check 1 0 ./t1 '1572045517 5922'

   t_epilog "${@}"
} # }}}

t_xcall() { # {{{
   t_prolog "${@}"

   if have_feat cmd-vexpr; then :; else
      t_echoskip '[!CMD_VEXPR]'
      t_epilog "${@}"
      return
   fi

   #{{{
   ${cat} <<- '__EOT' | \
      ${MAILX} ${ARGS} -Snomemdebug -Smax=${LOOPS_MAX} > ./t1 2>&1
	define work {
		echon "$1 "
      \if "$3" == ""; local set l=local; else; local set l=;\endif
		eval $l vput vexpr i + $1 1
		if $i -le "$max"
			eval $l vput vexpr j & $i 7
			if $j -eq 7
				echo .
			end
			eval $l \xcall work \"$i\" \"$2\" $l
		end
		echo ! The end for $1/$2
		if "$2" != ""
			return $i $^ERR-BUSY
		end
	}
	define xwork {
		\xcall work 0 $2
	}
	call work 0
	echo 1: ?=$? !=$!
	call xwork
	echo 2: ?=$? !=$!
	local xcall xwork
	echo 3: ?=$? !=$^ERRNAME
	#
	local call work 0 yes
	echo 4: ?=$? !=$^ERRNAME
	call xwork 0 yes
	echo 5: ?=$? !=$^ERRNAME
	__EOT
   #}}}
   i=${?}
   if [ ${LOOPS_MAX} -eq ${LOOPS_BIG} ]; then
      check_ex0 1-${LOOPS_BIG} ${i}
      check 1-${LOOPS_BIG} - ./t1 '2492586545 47176'
   else
      check_ex0 1-${LOOPS_SMALL} ${i}
      check 1-${LOOPS_SMALL} - ./t1 '2786527999 3909'
   fi

   ##

   if have_feat uistrings; then
      #{{{
      ${cat} <<- '__EOT' > ./t.in
		\define __w {
			\echon "$1 "
			local \ vput vexpr i + $1 1
			\if $i -le 111
				\local vput vexpr j & $i 7
				\if $j -eq 7; \echo .; \end
				\xcall __w $i $2
			\end
			\echo ! The end for $1
			\if $2 -eq 0
				nonexistingcommand
				\echo would be err with errexit
				\return
			\end
			\echo calling exit
			\exit
		}
		\define work {
			\echo eins
			\call __w 0 0
			\echo zwei, ?=$? !=$!
			\local set errexit
			\ignerr call __w 0 0
			\echo drei, ?=$? !=$^ERRNAME
			\call __w 0 $1
			\echo vier, ?=$? !=$^ERRNAME, this is an error
		}
		\ignerr call work 0
		\echo outer 1, ?=$? !=$^ERRNAME
		xxxign \call work 0
		\echo outer 2, ?=$? !=$^ERRNAME, could be error if xxxign non-empty
		\call work 1
		\echo outer 3, ?=$? !=$^ERRNAME
		\echo this is definitely an error
		__EOT
      #}}}
      < ./t.in ${MAILX} ${ARGS} -X'commandalias xxxign ignerr' \
         -Snomemdebug > ./t2 2>&1
      check 2 0 ./t2 '4036613316 4184'

      < ./t.in ${MAILX} ${ARGS} -X'commandalias xxxign " "' \
         -Snomemdebug > ./t3 2>&1
      check 3 1 ./t3 '3179757785 2787'
   else
      t_echoskip '2-3:[!UISTRINGS]'
   fi

   t_epilog "${@}"
} # }}}

t_local_x_call_environ() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
   \commandalias show '\
      \vput environ x lookup DEAD;\
      \if "$DEAD" != dead.0 || "$x" != dead.0;\echo 1:$DEAD:$x;\end;\
      \vput environ x lookup U;\
      \if "$U" != u.0 || "$x" != u.0;\echo 2 env[U=$x] U=$U;\end;\
      \vput environ x lookup N;\
      \if $? -eq 0;\echo 3:$N;\end;\
      \if "$N" != n.0;\echo 4:$N;\end'
   define l2 {
      echo ----${1}l2
      show
   }
   define l1 {
      eval $1 set DEAD=dead.0 U=u.0 N=n.0 x
      echo --l1
      show
      call l2
      xcall l2 $1
   }
   define xl0 {
      eval $1 xcall l1 "$2"
   }
   define l0 {
      local call l1 "$1"
   }
   define xi {
      set DEAD=dead.$1 U=u.$1 N=n.$1
   }
   define xo {
      echo -top-${1}
      echo DEAD=$DEAD U=$U N=$N;varshow DEAD U N
      !echo shell" DEAD<$DEAD> U<$U> N<$N>"
   }
   commandalias xi call xi
   commandalias xo call xo

   environ unset N
   set U=u.0
   environ link U

   xi 1;call l1;xo 1
   xi 2;local call l1;xo 2
   xi 3;call l1 local;xo 3

   xi 4;call xl0;xo 4
   xi 5;call xl0 local;xo 5
   xi 6;call xl0 '' local;xo 6
   xi 7;call xl0 local local;xo 7

   xi 8;call l0;xo 8
   xi 9;call l0 local;xo 9
	__EOT
   #}}}
   check 1 0 ./t1 '368283413 1412'

   t_epilog "${@}"
} # }}}

t_vpospar() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
   commandalias x echo '$?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>'
   commandalias y echo 'infun:$?/$^ERRNAME/$#:$*/"$@"/<$1><$2><$3><$4>'
   vpospar set hey, "'you    ", world!
   x
   vput vpospar x quote; echo x<$x>
   vpospar clear;x
   vput vpospar y quote;echo y<$y>
   eval vpospar set ${x};x
   eval vpospar set ${y};x
   eval vpospar set ${x};x

   define infun2 {
      echo infun2:$?/$^ERRNAME/$#:$*/"$@"/<$1><$2><$3><$4>
      vput vpospar z quote;echo infun2:z<$z>
   }

   define infun {
      y
      vput vpospar y quote;echo infun:y<$y>
      eval vpospar set ${x};y
      vpospar clear;y
      eval call infun2 $x
      y
      eval vpospar set ${y};y
   }

   call infun This "in a" fun
   x
   vpospar clear;x
	__EOT
   #}}}
   check 1 0 ./t1 '155175639 866'

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./tifs 2>&1
   commandalias x echo '$?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>'
   set ifs=\'
   echo ifs<$ifs> ifs-ws<$ifs-ws>
   vpospar set hey, "'you    ", world!
   x
   vput vpospar x quote; echo x<$x>
   vpospar clear;x
   eval vpospar set ${x};x

   set ifs=,
   echo ifs<$ifs> ifs-ws<$ifs-ws>
   vpospar set hey, "'you    ", world!
   unset ifs;x
   set ifs=,
   vput vpospar x quote; echo x<$x>
   vpospar clear;x
   eval vpospar set ${x};\
      unset ifs;x

   set ifs=$',\t'
   echo ifs<$ifs> ifs-ws<$ifs-ws>
   vpospar set hey, "'you    ", world!
   unset ifs; x
   set ifs=$',\t'
   vput vpospar x quote; echo x<$x>
   vpospar clear;x
   eval vpospar set ${x};\
   unset ifs;x
	__EOT
   #}}}
   check ifs 0 ./tifs '2015927702 706'

   #{{{
   ${MAILX} ${ARGS} -X '
      commandalias x echo '"'"'$?: $#: <$*>: <$1><$2><$3><$4><$5><$6>'"'"'
      set x=$'"'"'a b\nc d\ne f\n'"'"'
      vpospar set $x
      x
      eval vpospar set $x
      x
      set ifs=$'"'"'\n'"'"'
      eval vpospar set $x
      x
      unset ifs
      vput vpospar i quote
      x
      vpospar clear
      x
      echo i<$i>
      eval vpospar set $i
      x
      xit
      ' > ./tifs-2 2>&1
   #}}}
   check ifs-2 0 ./tifs-2 '1412306707 260'

   #{{{
   ${MAILX} ${ARGS} -X '
      commandalias x echo '"'"'$#: <$1><$2><$3>'"'"'
      set x=$'"'"'a b\n#c d\ne f\n'"'"'
      set ifs=$'"'"'\n'"'"'; vpospar set $x; unset ifs
      x
      set ifs=$'"'"'\n'"'"'; eval vpospar set $x; unset ifs
      x
      set ifs=$'"'"'\n'"'"'; vpospar evalset $x; unset ifs
      x
      vpospar evalset ""
      x
      vpospar evalset "a b c"
      x
      xit
      ' > ./tevalset 2>&1
   #}}}
   check evalset 0 ./tevalset '2054446552 79'

   #{{{
   ${MAILX} ${ARGS} -X '
      commandalias x echo '"'"'$?: $#: <$1><$2><$3><$4> x<$x>'"'"'
      define t1 {
         x
         global vpospar set a1 b1 c1 d1
         x
      }
      define t2 {
         x
         global local vput vpospar x quote
         x
         echo "x <$x>"
         local vput vpospar x quote
         x
         echo "x <$x>"
      }
      x
      vpospar set a b c d
      x
      call t1 t1.1 t1.2 t1.3 t1.4
      x
      call t2 t2.1 t2.2 t2.3 t2.4
      x
      local call t2 t3.1 t3.2 t3.3 t3.4
      x
      ' > ./tglobal 2>&1
   #}}}
   check global 0 ./tglobal '2494928013 543'

   t_epilog "${@}"
} # }}}

t_atxplode() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} > ./t.sh <<- '___'; ${cat} > ./t.rc <<- '___'
	x() { echo $#; }
	xxx() {
	  printf " (1/$#: <$1>)"
	  shift
	  if [ $# -gt 0 ]; then
	    xxx "$@"
	  else
	    echo
	  fi
	}
	yyy() {
	  eval "$@ ' ball"
	}
	set --
	x "$@"
	x "$@"''
	x " $@"
	x "$@ "
	printf yyy;yyy 'xxx' "b\$'\t'u ' "
	printf xxx;xxx arg ,b      u.
	printf xxx;xxx arg ,  .
	printf xxx;xxx arg ,ball.
	___
	define x {
	  echo $#
	}
	define xxx {
	  echon " (1/$#: <$1>)"
	  shift
	  if $# -gt 0; \xcall xxx "$@"; endif
	  echo
	}
	define yyy {
	  eval "$@ ' ball"
	}
	vpospar set
	call x "$@"
	call x "$@"''
	call x " $@"
	call x "$@ "
	echon yyy;call yyy '\call xxx' "b\$'\t'u ' "
	echon xxx;call xxx arg ,b      u.
	echon xxx;call xxx arg ,  .
	echon xxx;call xxx arg ,ball.
	___
   #}}}
   ${MAILX} ${ARGS} -X'source ./t.rc' -Xx > ./t1 2>&1
   check 1 0 ./t1 '41566293 164'

   #${SHELL} ./t.sh > ./t1disproof 2>&1
   #check 1disproof 0 ./t1disproof '41566293 164'

   t_epilog "${@}"
} # }}}

t_read() { # {{{
   t_prolog "${@}"

   ${cat} <<- '__EOT' > ./t1in
   hey1, "'you    ", world!
   hey2, "'you    ", bugs bunny!
   hey3, "'you    ",     
   hey4, "'you    "
	__EOT

   ${cat} <<- '__EOT' |\
      ${MAILX} ${ARGS} -X'readctl create ./t1in' > ./t1 2>&1
	commandalias x echo '$?/$^ERRNAME / <$a><$b><$c>'
   read a b c;x
   read a b c;x
   read a b c;x
   read a b c;x
   unset a b c;read a b c;x
   readctl remove ./t1in;echo readctl remove:$?/$^ERRNAME
	__EOT

   check 1 0 ./t1 '1527910147 173'

   ${cat} <<- '__EOT' > ./tifsin
   hey2.0,:"'you    ",:world!:mars.:
   hey2.1,:"'you    ",:world!
   hey2.2,:"'you    ",:bugs bunny!
   hey2.3,:"'you    ",:    
   hey2.4,:"'you    ":
   :
	__EOT

   ${cat} <<- '__EOT' |\
      6< ./tifsin ${MAILX} ${ARGS} -X 'readctl create 6' > ./tifs 2>&1
	commandalias x echo '$?/$^ERRNAME / <$a><$b><$c>'
   set ifs=:
   read a b c;x
   read a b c;x
   read a b c;x
   read a b c;x
   read a b c;x
   read a b c;x
   unset a b c;read a b c;x
   read a b c;x
   readctl remove 6;echo readctl remove:$?/$^ERRNAME
	__EOT

   check ifs 0 ./tifs '890153490 298'

   {
      echo 'hey1.0,:'"'"'you    ",:world!:mars.	'
      echo 'hey2.0,:'"'"'you    ",:world!:mars.:	'
      echo 'hey3.0,:'"'"'you    ",:world!:mars.::	'
      echo 'hey1.0,:'"'"'you    ",:world!:mars.	'
      echo 'hey2.0,:'"'"'you    ",:world!:mars.:	'
      echo 'hey3.0,:'"'"'you    ",:world!:mars.::	'
   } > ./tifsin-2

   ${MAILX} ${ARGS} -X '
      commandalias r read
      commandalias x echo \$?/\$^ERRNAME
      commandalias y x <\$a><\$b><\$c><\$d><\$e>
      define x {
         local set v=$*
         readctl creat ./tifsin-2;x
         set ifs=":	";eval r $v;unset ifs;y
         set ifs=":	";eval r $v;unset ifs;y
         set ifs=":	";eval r $v;unset ifs;y
         set ifs=:;eval r $v;unset ifs;y
         set ifs=:;eval r $v;unset ifs;y
         set ifs=:;eval r $v;unset ifs;y
         readctl remo ./tifsin-2;x
      }
      call x a b c
      call x a b c d
      call x a b c d e
      xit
      ' > ./tifs-2
   check ifs-2 0 ./tifs-2 '3840749226 897'

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./treadall 2>&1
	commandalias x echo '$?/$^ERRNAME / <$d>'
   readctl create ./t1in
   readall d;x
   set d;readall d;x
   readctl create tifsin
   readall d;x
   set d;readall d;x
   readctl remove ./t1in;echo $?/$^ERRNAME;\
      readctl remove tifsin;echo $?/$^ERRNAME
   echo '### now with empty lines'
   ! printf 'one line\n\ntwo line\n\n' > ./temptynl
   readctl create ./temptynl;echo $?/$^ERRNAME
   readall d;x
   readctl remove ./temptynl;echo $?/$^ERRNAME
	__EOT
   check readall 0 ./treadall '4113506527 405'

   {
      echo abra
      echo kadabra
   } > ./tlocalin

   ${MAILX} ${ARGS} -X '
      commandalias r read
      commandalias x echo \$?/\$^ERRNAME
      define x {
         echo ==$1
         readctl creat ./tlocalin;x
         local eval $1 locvar;x;echo "L<$locvar>"
         readctl remo ./tlocalin;x
      }
      set locvar=run
      call x read;echo "G<$locvar>"
      call x readall;echo "G<$locvar>"
      xit
      ' > ./tlocal
   check local 0 ./tlocal '293471176 99'

   t_epilog "${@}"
} # }}}

t_readsh() { # {{{ TODO not enough
   t_prolog "${@}"

   ${cat} <<- '__EOT' > ./t1in
   from@exam.ple    ' diet spliced <from@exam.ple>   '    'a' 
   from@exam.ple ' diet spliced <from@exam.ple>   ' 'a'  
   from@exam.ple ' diet spliced <from@exam.ple>   ''a'  
   from@exam.ple' diet spliced <from@exam.ple>   ''a'  
	__EOT

   ${cat} <<- '__EOT' |\
      ${MAILX} ${ARGS} -X'readctl create ./t1in' > ./t1 2>&1
   commandalias x echo '$?/$^ERRNAME / <$a><$b><$c>'
   readsh a b c;x
   readsh a b c;x
   readsh a b c;x
   readsh a b c;x
   unset a b c;read a b c;x
   readctl remove ./t1in;echo readctl remove:$?/$^ERRNAME
	__EOT
   check 1 0 ./t1 '2955084684 291'

   {
      echo abra
      echo kadabra
   } > ./tlocalin

   ${MAILX} ${ARGS} -X '
      commandalias r read
      commandalias x echo \$?/\$^ERRNAME
      define x {
         readctl creat ./tlocalin;x
         local readsh locvar;x;echo "L<$locvar>"
         readctl remo ./tlocalin;x
      }
      set locvar=run
      call x;echo "G<$locvar>"
      xit
      ' > ./tlocal
   check local 0 ./tlocal '2737640059 36'

   t_epilog "${@}"
} # }}}

t_fop() { # XXX improve writes when we have redirection {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -SCAT=${cat} > ./t1 2>&1
commandalias x echo '$?/$^ERRNAME :$res:'
echo ===T1
vput fop res touch ./t1.1;x
unset res;vput fop res_noecho stat ./t1.1;x
echo ===T2
vput fop res lock ./t1.2 w;x
set fd=$res
#xxx write on our own
! (echo l1;echo l2;echo l3;) > ./t1.2
echo ===readctl create 2.1
readctl create $fd;x
read res;x
read res;x
read res;x
read res;x
echo ===rewind 2.1.1
vput fop res rewind $fd;x
read res;x
read res;x
read res;x
read res;x
echo ===rewind 2.1.2
vput fop res rewind $fd;x
vput fop res pass $fd @ "${CAT} && exit 11";x
vput fop res pass $fd - "${CAT} && exit 12";x
echo ===rewind 2.1.3
vput fop res rewind $fd;x
vput fop res pass $fd - "${CAT} && exit 13";x
vput fop res pass $fd - "${CAT} && exit 14";x
echo ===dtors 2.1
vput fop res close $fd;x
vput fop res close $fd;x
readctl remove $fd;x
	__EOT
   #}}}
   check 1 0 ./t1 '2608383886 367'

   if have_feat flock; then
      #{{{
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -SCAT=${cat} > ./t2 2>&1
commandalias x echo '$?/$^ERRNAME :$res:'
echo ===T1
vput fop res flock ./t2.1 a 'echo x1;echo x2;echo x3';x
vput fop res flock ./t2.1 r;x
set fd=$res
echo ===readctl create 1.1
readctl create $fd;x
echo ===rewind 1.1.1
vput fop res rewind $fd;x
read res;x
read res;x
read res;x
read res;x
echo ===rewind 1.1.2
vput fop res rewind $fd;x
vput fop res pass $fd @ "${CAT} && exit 21";x
vput fop res pass $fd - "${CAT} && exit 22";x
echo ===rewind 1.1.3
vput fop res rewind $fd;x
vput fop res pass $fd - "${CAT} && exit 23";x
vput fop res pass $fd - "${CAT} && exit 24";x
echo ===dtors 1.1
vput fop res close $fd;x
vput fop res close $fd;x
readctl remove $fd;x
		__EOT
      #}}}
      check 2 0 ./t2 '1544976144 297'
   else
      t_echoskip '2:[!FLOCK]'
   fi

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -SCAT=${cat} > ./t3 2>&1
commandalias x echo '$?/$^ERRNAME :$res:'
echo ===T1
vput fop fd open ./t3.x w;x 1
vput fop res open ./t3.x W;x 2
fop pass - $fd "echo 1;echo .2;echo ._3";x 3 #xxx write on our own
vput fop nil rewind $fd;x 4
readctl create $fd;x 5
read res;x 6
read res;x 7
read res;x 8
read res;x 9
vput fop nil close $fd;x 10
readctl remove $fd;x 11
echo ===T2
vput fop fd open ./t3.x w;x 20
fop pass - $fd "echo X";x 21 #xxx write on our own
vput fop nil rewind $fd;x 22
readctl create $fd;x 23
read res;x 24
read res;x 25
read res;x 26
read res;x 27
readctl remove $fd;x 28
vput fop nil close $fd;x 29
echo ===T3
vput fop fd open ./t3.x a^;x 30
vput fop res open ./t3.x A;x 31
fop pass - $fd "echo ._,4";x 32 #xxx write on our own
fop rewind $fd;x 23
readctl create $fd;x 34
read res;x 35
read res;x 36
read res;x 37
read res;x 38
read res;x 39
readctl remove $fd;x 40
fop close $fd;x 41
echo ===T4
vput fop fd open ./t3.x a0^;x 50
fop pass - $fd "echo 123";x 51 #xxx write on our own
fop rewind $fd;x 52
readctl create $fd;x 53
read res;x 54
read res;x 55
readctl remove $fd;x 56
fop close $fd;x 57
	__EOT
   #}}}
   check 3 0 ./t3 '3318177702 649'

   t_epilog "${@}"
} # }}}

t_msg_number_list() { # {{{
   t_prolog "${@}"

   #{{{
   {
      t__gen_msg from 'ex1@am.ple' subject sub1
      t__gen_msg from 'ex2@am.ple' subject sub2
      t__gen_msg from 'ex3@am.ple' subject sub3
   } > ./t.mbox

   ${MAILX} ${ARGS} -Rf -Y '#
      commandalias x echo '"'"'$?/$^ERRNAME <$res>'"'"'
      set res
      =
      x
      = +
      x
      # (dot not moved)
      = +
      x
      = $
      x
      = ^
      x
      vput = res
      x
      vput = res *
      x
      set ifs=","
      vput = res *
      x
      set ifs=", "
      vput = res *
      x
      xit
      ' ./t.mbox > ./t1 2>>${ERR}
   #}}}
   check 1 0 ./t1 '3152029378 118'

   t_epilog "${@}"
} # }}}
# }}}

# Send/RFC absolute basics {{{
t_addrcodec() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
	commandalias x echo '$?/$^ERRNAME $res'
	vput addrcodec res e 1 <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res e 2 . <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res e 3 Sauer Dr. <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res e 3.50 Sauer (Ma) Dr. <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res e 3.51 Sauer (Ma) "Dr." <doog@def>
	x
	eval vput addrcodec res d $res
	x
	#
	vput addrcodec res +e 4 Sauer (Ma) Dr. <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 5 Sauer (Ma) Braten Dr. <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 6 Sauer (Ma) Braten Dr. (Heu) <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 7 Sauer (Ma) Braten Dr. (Heu) <doog@def> (bu)
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 8 \
		Dr. Sauer (Ma) Braten Dr. (Heu) <doog@def> (bu) Boom. Boom
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 9 Dr.Sauer(Ma)Braten Dr. (Heu) <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 10 (Ma)Braten Dr. (Heu) <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 11 (Ma)Braten Dr"." (Heu) <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 12 Dr.     Sauer  (Ma)   Braten    Dr.   (u) <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 13(Ma)Braten    Dr.     (Heu)     <doog@def>
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 14 Hey, Du <doog@def> Wie() findet Dr. das? ()
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 15 \
		Hey, Du <doog@def> Wie() findet "" Dr. "" das? ()
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 16 \
		"Hey," "Du" <doog@def> "Wie()" findet "" Dr. "" das? ()
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 17 \
		"Hey" Du <doog@def> "Wie() findet " " Dr. """ das? ()
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 18 \
		<doog@def> "Hey" Du "Wie() findet " " Dr. """ das? ()
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res +e 19 Hey\,\"  <doog@def> "Wie()" findet \" Dr. \" das?
	x
	eval vput addrcodec res d $res
	x
	#
	vput addrcodec res ++e 20 Hey\,\"  <doog@def> "Wie()" findet \" Dr. \" das?
	x
	vput addrcodec res ++e 21 Hey\,\""  <doog@def> "Wie()" findet \" Dr. \" das?
	x
	eval vput addrcodec res d $res
	x
	#
	vput addrcodec res \
	   +++e 22 Hey\\,\"  <doog@def> "Wie()" findet \" Dr. \" das?
	x
	eval vput addrcodec res d $res
	x
	#
	vput addrcodec res s \
		"23 Hey\\,\\\" \"Wie" () "\" findet \\\" Dr. \\\" das?" <doog@def>
	x
	#
	# Fix for [f3852f88]
	vput addrcodec res ++e <from2@exam.ple> 100 (comment) "Quot(e)d"
	x
	eval vput addrcodec res d $res
	x
	vput addrcodec res e <from2@exam.ple> 100 (comment) "Quot(e)d"
	x
	eval vput addrcodec res d $res
	x
	__EOT
   #}}}
   check 1 0 ./t1 '1047317989 2612'

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t2 2>&1
	commandalias x echo '$?/$^ERRNAME $res'
   mlist isa1@list
   mlsubscribe isa2@list
   #
   vput addrcodec res skin Hey\\,\"  <isa0@list> "Wie()" find \" Dr. \" das?
   x
   vput addrcodec res skinlist \
      Hey\\,\"  <isa0@list> "Wie()" find \" Dr. \" das?
   x
   vput addrcodec res skin Hey\\,\"  <isa1@list> "Wie()" find \" Dr. \" das?
   x
   vput addrcodec res skinlist \
      Hey\\,\"  <isa1@list> "Wie()" find \" Dr. \" das?
   x
   vput addrcodec res skin Hey\\,\"  <isa2@list> "Wie()" find \" Dr. \" das?
   x
   vput addrcodec res skinlist \
      Hey\\,\"  <isa2@list> "Wie()" find \" Dr. \" das?
   x
	__EOT
   #}}}
   check 2 0 ./t2 '1391779299 104'

   if have_feat idna; then
      #{{{
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} ${ADDARG_UNI} > ./tidna 2>&1
		commandalias x echo '$?/$^ERRNAME $res'
      vput addrcodec res e    (heu) <du@blödiän> "stroh" du   
      x
      eval vput addrcodec res d $res
      x
      vput addrcodec res e       <du@blödiän>   du     
      x
      eval vput addrcodec res d $res
      x
      vput addrcodec res e     du    <du@blödiän>   
      x
      eval vput addrcodec res d $res
      x
      vput addrcodec res e        <du@blödiän>    
      x
      eval vput addrcodec res d $res
      x
      vput addrcodec res e        du@blödiän    
      x
      eval vput addrcodec res d $res
      x
		__EOT
      #}}}
      check idna 0 ./tidna '498775983 326'
   else
      t_echoskip 'idna:[!IDNA]'
   fi

   t_epilog "${@}"
} # }}}

t_headerpick() { # {{{
   t_prolog "${@}"

   t__x1_msg > ./tmbox

   #{{{
   </dev/null ${MAILX} ${ARGS} -Rf -Y '# Do not care much on error UISTRINGS
commandalias x \echo '"'"'--- $?/$^ERRNAME, '"'"'
\echo --- 1
\headerpick
x2
\type
x3
\if "$features" !% +uistrings,
   \echoerr reproducible_build: Invalid field name cannot be ignored: ba:l
\endif
\headerpick type ignore \
   from_ mail-followup-to in-reply-to DATE MESSAGE-ID STATUS ba:l
x4
\if "$features" !% +uistrings,
   \echo "#headerpick type retain currently covers no fields"
\endif
\headerpick
x5
\type
x6
\unheaderpick type ignore from_ DATE STATUS
x7
\if "$features" !% +uistrings,
   \echo "#headerpick type retain currently covers no fields"
\endif
\headerpick
x8
\type
x9
\if "$features" =% +uistrings,
   \unheaderpick type ignore from_ ba:l
   \set x=$? y=$^ERRNAME
\else
   \echoerr reproducible_build: Field not ignored: from_
   \echoerr reproducible_build: Field not ignored: ba:l
   \set x=1 y=INVAL
\endif
\echo --- $x/$y, 10
\unheaderpick type ignore *
x11
\if "$features" !% +uistrings,
   \echo "#headerpick type retain currently covers no fields"
   \echo "#headerpick type ignore currently covers no fields"
\endif
\headerpick
x12
\type
\echo --- $?/$^ERRNAME, 13 ---
#  ' ./tmbox > ./t1 2>&1
   #}}}
   check 1 0 ./t1 '2481904228 2273'

   if have_feat uistrings; then
      #{{{
      have_feat regex && i='3515512395 2378' || i='4201290332 2378'
      </dev/null ${MAILX} ${ARGS} -Y '#
commandalias x \echo '"'"'--- $?/$^ERRNAME, '"'"'
\headerpick type retain \
   bcc cc date from sender subject to \
   message-id mail-followup-to reply-to user-agent
x1
\headerpick forward retain \
   cc date from message-id list-id sender subject to \
   mail-followup-to reply-to
x2
\headerpick save ignore ^Original-.*$ ^X-.*$ ^DKIM.*$
x3
\headerpick top retain To Cc
\echo --- $?/$^ERRNAME, 4 ---
\headerpick
x5
\headerpick type
x6
\headerpick forward
x7
\headerpick save
x8
\headerpick top
\echo --- $?/$^ERRNAME, 9 ---
\unheaderpick type retain message-id mail-followup-to reply-to user-agent
x10
\unheaderpick save ignore ^X-.*$ ^DKIM.*$
x11
\unheaderpick forward retain *
\echo --- $?/$^ERRNAME, 12 ---
\headerpick
x13
\headerpick type
x14
\headerpick save
\echo --- $?/$^ERRNAME, 15 --
\unheaderpick type retain *
x16
\unheaderpick forward retain *
x17
\unheaderpick save ignore *
x18
\unheaderpick top retain *
\echo --- $?/$^ERRNAME, 19 --
\headerpick
x20
#  ' > ./t2 2>&1
      #}}}
      check 2 0 ./t2 "${i}"

      # {{{object
      </dev/null ${MAILX} ${ARGS} -Y '
commandalias x \echo '"'"'--- $?/$^ERRNAME, '"'"'
headerp create au;x1
headerp create au1;x2
headerp create au2;x3
headerp create au3;x4
headerp create au;x5
headerp create .au;x6
headerp;x7
headerp au ret add bla1;x8
headerp au1 ign add bla2;x9
headerp ass au2 au;x10
headerp ass au3 au1;x11
headerp;x12
headerp joi au2 au3;x13
headerp joi au3 au;x14
headerp;x15
headerp rem au3;x16
headerp rem au;x17
headerp rem au1;x18
headerp rem au2;x19
headerp;x20
#
headerp crea au1;x21
headerp crea au2;x22
headerp au1 ign add du du du du du du du au1;x23
headerp au2 ign add au2 from from from from from;x24
headerp joi au1 au2;x25
headerp;x26
      ' > ./t3 2>&1
      #}}}
      check 3 0 ./t3 '3491122476 1689'
   else
      t_echoskip '2-3:[!UISTRINGS]'
   fi

   t_epilog "${@}"
} # }}}

t_can_send_rfc() { # {{{
   t_prolog "${@}"

   </dev/null ${MAILX} ${ARGS} -Smta=test://./t.mbox -s Sub.1 \
      receiver@number.1 \
      > ./t1 2>&1
   check 1 0 ./t.mbox '550126528 126'
   check 1-err - ./t1 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=test://./t.mbox -s Sub.2 \
      -b bcc@no.1 -b bcc@no.2 -b bcc@no.3 \
      -c cc@no.1 -c cc@no.2 -c cc@no.3 \
      to@no.1 to@no.2 to@no.3 \
      > ./t2 2>&1
   check 2 0 ./t.mbox '3259888945 324'
   check 2-err - ./t2 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=test://./t.mbox -s Sub.2no \
      -b bcc@no.1\ \ bcc@no.2 -b bcc@no.3 \
      -c cc@no.1,cc@no.2 -c cc@no.3 \
      to@no.1,to@no.2 to@no.3 \
      > ./t2no 2>&1
   check 2no 4 ./t.mbox '3350946897 468'
   if have_feat uistrings; then
      check 2no-err - ./t2no '3397557940 190'
   else
      check 2no-err - ./t2no '4294967295 0'
   fi

   # XXX NOTE we cannot test "cc@no1 <cc@no.2>" because our stupid parser
   # XXX would not treat that as a list but look for "," as a separator
   </dev/null ${MAILX} ${ARGS} -Smta=test://./t.mbox -Sfullnames -s Sub.3 \
      -T 'bcc?single: bcc@no.1, <bcc@no.2>' -T bcc:\ bcc@no.3 \
      -T cc?si\ \ :\ \ 'cc@no.1, <cc@no.2>' -T cc:\ cc@no.3 \
      -T to?:\ to@no.1,'<to@no.2>' -T to:\ to@no.3 \
      > ./t3 2>&1
   check 3 0 ./t.mbox '1453534480 678'
   check 3-err - ./t3 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=test://./t.mbox -Sfullnames -s Sub.4 \
      -T 'bcc: bcc@no.1, <bcc@no.2>' -T bcc:\ bcc@no.3 \
      -T cc:\ 'cc@no.1, <cc@no.2>' -T cc\ \ :\ \ cc@no.3 \
      -T to\ :to@no.1,'<to@no.2>' -T to:\ to@no.3 \
      > ./t4 2>&1
   check 4 0 ./t.mbox '535767201 882'
   check 4-err - ./t4 '4294967295 0'

   # Two test with a file-based MTA
   ${cat} <<-_EOT > ./tmta.sh
		#!${SHELL} -
		(echo 'From reproducible_build Wed Oct  2 01:50:07 1996' &&
			${cat} && echo pardauz && echo) > ./t.mbox
	_EOT
   ${chmod} 0755 ./tmta.sh

   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s Sub.mta-1 \
      receiver@number.1 > ./t5 2>&1
   check 5 0 ./t.mbox '2384401657 138'
   check 5-err - ./t5 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=file://./tmta.sh -s Sub.mta-2 \
      receiver@number.1 > ./t6 2>&1
   check 6 0 ./t.mbox '3006460737 138'
   check 6-err - ./t6 '4294967295 0'

   # Command
   </dev/null ${MAILX} ${ARGS} -Smta=test \
      -Y '#
mail hey@exam.ple
~s Subject 1
Body1
~.
echo $?/$^ERRNAME
xit
# '   > ./t7 2>&1
   check 7 0 ./t7 '951018449 138'

   ## *record*, *outfolder*, with and without *mta-bcc-ok*
   ${mkdir} tfolder
   xfolder=`${pwd}`/tfolder

   ${cat} <<-_EOT > ./tmta.sh
		#!${SHELL} -
		(echo 'From reproducible_build Wed Oct  2 01:50:07 1996' &&
			${cat} && echo 'ARGS: '"\${@}" && echo) > ./t.mbox
	_EOT
   ${chmod} 0755 ./tmta.sh

   t_it() {
      tno=$1
      shift
      </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -Sfolder="${xfolder}" \
         "${@}" \
         -s Sub.mta-1 \
         -b bcc@no.1 -b bcc@no.2 -b bcc@no.3 \
         -c cc@no.1 -c cc@no.2 -c cc@no.3 \
         to@no.1 to@no.2 to@no.3 \
         receiver@number.1 > ./t$tno 2>&1
      return ${?}
   }

   t_it 8 -Snomta-bcc-ok
   check 8 0 ./t.mbox '1365032629 292'
   check 8-1 - ./t8 '4294967295 0'

   t_it 9 -Snomta-bcc-ok -Srecord=trec9
   check 9 0 ./t.mbox '1365032629 292'
   check 9-1 - ./t9 '4294967295 0'
   check 9-2 - ./trec9 '160206230 221'

   t_it 10 -Srecord=./trec10
   check 10 0 ./t.mbox '3085765596 326'
   check 10-1 - ./t10 '4294967295 0'
   check 10-2 - ./trec10 '160206230 221'

   t_it 11 -Snomta-bcc-ok -Srecord=trec11_12 -Soutfolder
   check 11 0 ./t.mbox '1365032629 292'
   check 11-1 - ./t11 '4294967295 0'
   check 11-2 - ./tfolder/trec11_12 '160206230 221'
   # That is appends to an MBOX
   t_it 12 -Srecord=trec11_12 -Soutfolder
   check 12 0 ./t.mbox '3085765596 326'
   check 12-1 - ./t12 '4294967295 0'
   check 12-2 - ./tfolder/trec11_12 '1618754846 442'

   ### More RFC cases

   ## From: and Sender:
   </dev/null ${MAILX} ${ARGS} -s ubject \
      -S from=a@b.org,b@b.org,c@c.org -S sender=a@b.org \
      to@exam.ple > ./t13 2>&1
   check 13 0 ./t13 '2837699804 203'

   # ..if From: is single mailbox and Sender: is same, no Sender:
   </dev/null ${MAILX} ${ARGS} -s ubject \
      -S from=a@b.org -S sender=a@b.org \
      to@exam.ple > ./t14 2>&1
   check 14 0 ./t14 '3373917180 151'

   # Ensure header line folding works out, with/out *mta-bcc-ok*
   </dev/null ${MAILX} ${ARGS} -S mta=./tmta.sh \
      -s ubject -Y 'Hi!' \
      -b bcc-long-name-that-causes-wrap@no.1 \
      -b bcc-long-name-that-causes-wrap@no.2 \
      -b bcc-long-name-that-causes-wrap@no.3 \
      -b bcc-long-name-that-causes-wrap@no.4 \
      -c cc-long-name-that-causes-wrap@no.1 \
      -c cc-long-name-that-causes-wrap@no.2 \
      -c cc-long-name-that-causes-wrap@no.3 \
      -c cc-long-name-that-causes-wrap@no.4 \
      to-long-name-that-causes-wrap@no.1 \
      to-long-name-that-causes-wrap@no.2 \
      to-long-name-that-causes-wrap@no.3 \
      to-long-name-that-causes-wrap@no.4 \
      > ./t15 2>&1
   check 15 0 ./t.mbox '3193007256 998'
   check 15-1 - ./t15 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -S mta=./tmta.sh -S nomta-bcc-ok \
      -s ubject -Y 'Hi!' \
      -b bcc-long-name-that-causes-wrap@no.1 \
      -b bcc-long-name-that-causes-wrap@no.2 \
      -b bcc-long-name-that-causes-wrap@no.3 \
      -b bcc-long-name-that-causes-wrap@no.4 \
      -c cc-long-name-that-causes-wrap@no.1 \
      -c cc-long-name-that-causes-wrap@no.2 \
      -c cc-long-name-that-causes-wrap@no.3 \
      -c cc-long-name-that-causes-wrap@no.4 \
      to-long-name-that-causes-wrap@no.1 \
      to-long-name-that-causes-wrap@no.2 \
      to-long-name-that-causes-wrap@no.3 \
      to-long-name-that-causes-wrap@no.4 \
      > ./t16 2>&1
   check 16 0 ./t.mbox '2175359047 843'
   check 16-1 - ./t16 '4294967295 0'

   t_epilog "${@}"
} # }}}

t_mta_args() { # {{{
   t_prolog "${@}"

   ${cat} <<-_EOT > ./tmta.sh
		#!${SHELL} -
		(
			echo '\$#='\${#}
			echo '\$0='\${0}
			while [ \${#} -gt 0 ]; do
				echo 'ARG<'"\${1}"'>'
				shift
			done
		) > ./t.mbox
	_EOT
   ${chmod} 0755 ./tmta.sh

   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s t \
      r@e.c > ./t1 2>&1
   check 1 0 ./t.mbox '137783094 45'
   check 1-err - ./t1 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s t \
      -S metoo \
      r@e.c > ./t2 2>&1
   check 2 0 ./t.mbox '2700843329 53'
   check 2-err - ./t2 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s t \
      -S metoo -S verbose \
      r@e.c > ./t3 2>&1
   check 3 0 ./t.mbox '2202430763 61'
   check 3-err - ./t3 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s t \
      -S metoo -S verbose -S mta-no-default-arguments \
      r@e.c > ./t4 2>&1
   check 4 0 ./t.mbox '2206079536 29'
   check 4-err - ./t4 '4294967295 0'

   #
   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s t \
      -S mta-no-default-arguments -S mta-no-recipient-arguments \
      r1@e.c r2@e.c > ./t5 2>&1
   check 5 0 ./t.mbox '2135964332 18'
   check 5-err - ./t5 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s t \
      -S mta-no-recipient-arguments \
      r1@e.c r2@e.c > ./t6 2>&1
   check 6 0 ./t.mbox '745357308 34'
   check 6-err - ./t6 '4294967295 0'

   # *mta-bcc-ok* tested in can_send_rfc()

   #
   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s t \
      -S mta-arguments='-t -X "/tmp/my log"' \
      r@e.c > ./t7 2>&1
   check 7 0 ./t.mbox '1146999542 78'
   check 7-err - ./t7 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=./tmta.sh -s t \
      -S mta-no-default-arguments \
      -S mta-arguments='-t -X "/tmp/my log"' \
      r@e.c > ./t8 2>&1
   check 8 0 ./t.mbox '2762392930 62'
   check 8-err - ./t8 '4294967295 0'

   # NOBATCH_!
   </dev/null ${MAILX} ${NOBATCH_ARGS} -Smta=./tmta.sh -s t \
      -S mta-arguments='-t -X "/tmp/my log"' \
      r@e.c \
      -- -x -y -z > ./t9 2>&1
   check_exn0 9
   check 9-err - ./t9 '3200702761 93'

   </dev/null ${MAILX} ${NOBATCH_ARGS} -Smta=./tmta.sh -s t \
      -S mta-arguments='-t -X "/tmp/my log"' \
      -S expandargv \
      r@e.c \
      -- -x -y -z > ./t10 2>&1
   check 10 0 ./t.mbox '3004936903 102'
   check 10-err - ./t10 '4294967295 0'

   </dev/null ${MAILX} ${NOBATCH_ARGS} -Smta=./tmta.sh -s t \
      -S mta-no-default-arguments \
      -S mta-arguments='-t -X "/tmp/my log"' \
      -S expandargv \
      r@e.c \
      -- -x -y -z > ./t11 2>&1
   check 11 0 ./t.mbox '1392240145 86'
   check 11-err - ./t11 '4294967295 0'

   </dev/null ${MAILX} ${NOBATCH_ARGS} -Smta=./tmta.sh -s t \
      -S expandargv=fail \
      r@e.c \
      -- -x -y -z > ./t12 2>&1
   check_exn0 12
   check 12-err - ./t12 '3200702761 93'

   </dev/null ${MAILX} ${NOBATCH_ARGS} -Smta=./tmta.sh -s t \
      -S expandargv=restrict \
      r@e.c \
      -- -x -y -z > ./t13 2>&1
   check_exn0 13
   check 13-err - ./t13 '3200702761 93'

   </dev/null ${MAILX} ${NOBATCH_ARGS} -Smta=./tmta.sh -s t \
      -S expandargv=restrict -~ \
      r@e.c \
      -- -x -y -z > ./t14 2>&1
   check 14 0 ./t.mbox '1330910444 69'
   check 14-err - ./t14 '4294967295 0'

   </dev/null ${MAILX} ${NOBATCH_ARGS} -Smta=./tmta.sh -s t \
      -S expandargv=restrict -# \
      r@e.c \
      -- -x -y -z > ./t15 2>&1
   check 15 0 ./t.mbox '1330910444 69'
   check 15-err - ./t15 '4294967295 0'

   # *mta-argv0* cannot be tested via shell

   ## Important to test other but send-only mode
   printf 'mail r@e.c\n~s t\n~.' |
      ${MAILX} ${NOBATCH_ARGS} -Smta=./tmta.sh -R \
      -S expandargv -# \
      -- -x -y -z > ./t16 2>&1
   check 16 0 ./t.mbox '1330910444 69'
   check 16-err - ./t16 '4294967295 0'

   t_epilog "${@}"
} # }}}

t_reply() { # {{{
   # Alternates and ML related address massage etc. somewhere else
   t_prolog "${@}"

   t__gen_msg subject reply from 1 to 2 cc 2 > "${MBOX}"

   ## Base (does not test "recipient record")
   t_it() {
      </dev/null ${MAILX} ${ARGS} -Rf \
         -Y "${2}${1}"'
r1
~.
      echo 1:$?/$^ERRNAME
      set fullnames escape=!; '${1}'
r2 fullnames
!.
      echo 2:$?/$^ERRNAME
      set recipients-in-cc nofullnames; '${1}'
r3 recipients-in-cc
!.
      echo 3:$?/$^ERRNAME
      unset recipients-in-cc; '${1}'
r4
!.
      echo 4:$?/$^ERRNAME
      #' \
         "${MBOX}" > ./.tall 2>&1
      return ${?}
   }

   t_it reply
   check 1 0 ./.tall '4164251531 851'
   t_it Reply
   check 2 0 ./.tall '3034955332 591'
   t_it reply 'set flipr;'
   check 3 0 ./.tall '3034955332 591'
   t_it Reply 'set flipr;'
   check 4 0 ./.tall '4164251531 851'

   ## Dig the errors
   t__gen_msg subject reply-no-addr > ./.tnoaddr

   # MBOX will deduce addressee from From_ line..
   </dev/null ${MAILX} ${ARGS} -R -Sescape=! \
      -Y '#
   File ./.tnoaddr; reply # Takes addressee from From_ line :(
body1
!.
   echo 1:$?/$^ERRNAME
   File '"${MBOX}"'; set ea=$expandaddr expandaddr=-all; reply
body2
!.
   echo 2:$?/$^ERRNAME; set expandaddr=$ea; reply 10 # BADMSG
   echo 3:$?/$^ERRNAME; reply # cannot test IO,NOTSUP,INVAL
body3
!.
   echo 4:$?/$^ERRNAME
   #' \
      > ./.tall 2>./.terr
   check 5 0 ./.tall '3088217220 382'
   if have_feat uistrings; then
      check 6 - ./.terr '2514745519 544'
   else
      t_echoskip '6:[!UISTRINGS]'
   fi

   # ..but Maildir will not
   if have_feat maildir; then
      ${mkdir} -p .tdir .tdir/tmp .tdir/cur .tdir/new
      ${sed} 1d < ./.tnoaddr > .tdir/new/sillyname

      </dev/null ${MAILX} ${ARGS} -R -Sescape=! \
         -Y '#
      File ./.tdir; reply
body1
!.
      echo 1:$?/$^ERRNAME
      File '"${MBOX}"'; set ea=$expandaddr expandaddr=-all; reply
body2
!.
      echo 2:$?/$^ERRNAME; set expandaddr=$ea; reply 10 # BADMSG
      echo 3:$?/$^ERRNAME;reply # cannot test IO,NOTSUP,INVAL
body3
!.
      echo 4:$?/$^ERRNAME
      #' \
         > ./.tall 2>./.terr
      check 7 0 ./.tall '3631170341 244'
      if have_feat uistrings; then
         check 8 - ./.terr '1074346767 629'
      else
         t_echoskip '8:[!UISTRINGS]'
      fi
   fi

   ## Ensure action on multiple messages
   t__gen_msg subject reply2 from from2@exam.ple body body2 >> "${MBOX}"

   t_it() {
      </dev/null ${MAILX} ${ARGS} -Rf -Sescape=! \
      -Y '#
      '${1}' 1 2
repbody1
!.
repbody2
!.
      echo 1:$?/$^ERRNAME; '${2}' 1 2
Repbody1
!.
      echo 2:$?/$^ERRNAME
      #' \
         "${MBOX}" > ./.tall 2>&1
      check ${3} 0 ./.tall '283309820 502'
      if [ ${#} -eq 4 ]; then
         echo * > ./.tlst
         check ${3}-1 - ./.tlst '1649520021 12'
         check ${3}-2 - ./from1 '1501109193 347'
         check ${3}-3 - ./from2 '2154231432 137'
      fi
   }

   t_it reply Reply 9
   t_it respond Respond 10
   t_it followup Followup 11 yes
   ${rm} -f from1 from2

   ## *record*, *outfolder* (reuses $MBOX)
   ${mkdir} .tfolder

   #{{{
   t_it() {
      </dev/null ${MAILX} ${ARGS} -Rf -Sescape=! -Sfolder=`${pwd}`/.tfolder \
      -Y '#
      '${1}' 1 2
repbody1
!.
repbody2
!.
      echo 1:$?/$^ERRNAME; '${2}' 1 2
Repbody3
!.
      echo 2:$?/$^ERRNAME; set record=.trec'${4}'; '${1}' 1 2
repbody4
!.
repbody5
!.
      echo 3:$?/$^ERRNAME; '${2}' 1 2
Repbody6
!.
      echo 4:$?/$^ERRNAME; set outfolder norecord
      '${1}' 1 2
repbody1
!.
repbody2
!.
      echo 1:$?/$^ERRNAME; '${2}' 1 2
Repbody3
!.
      echo 2:$?/$^ERRNAME; set record=.trec'${4}'; '${1}' 1 2
repbody4
!.
repbody5
!.
      echo 3:$?/$^ERRNAME; '${2}' 1 2
Repbody6
!.
      echo 4:$?/$^ERRNAME
      #' \
         "${MBOX}" > ./.tall 2>&1
      check ${3} 0 ./.tall '3410330303 2008'
      if [ ${#} -ne 5 ]; then
         check ${4} - ./.trec${4} '3044885336 484'
         check ${4}-1 - ./.tfolder/.trec${4} '3044885336 484'
      else
         [ -f ./.trec${4} ]; check_exn0 ${4}
         echo * > ./.tlst
         check ${4}-1 - ./.tlst '1649520021 12'
         check ${4}-2 - ./from1 '2668975631 694'
         check ${4}-3 - ./from2 '225462887 274'
         [ -f ./.tfolder/.trec${4} ]; check_exn0 ${4}-4
         ( cd .tfolder && echo * > ./.tlst )
         check ${4}-5 - ./.tfolder/.tlst '1649520021 12'
         check ${4}-6 - ./.tfolder/from1 '2668975631 694'
         check ${4}-7 - ./.tfolder/from2 '225462887 274'
      fi
   }
   #}}}

   t_it reply Reply 12 13
   t_it respond Respond 14 15
   t_it followup Followup 16 17 yes
   #${rm} -f from1 from2

   #{{{ Quoting (if not cmd_escapes related)
   ${rm} -f "${MBOX}"
   t__x2_msg > ./.tmbox

   printf '#
      set indentprefix=" |" quote
      reply
b1
!.
      set quote=noheading quote-inject-head
      reply
b2
!.
      headerpick type retain cc date from message-id reply-to subject to
      set quote=headers
      reply
b3
!.
      set quote=allheaders
      reply
b4
!.
      set quote-inject-head=%% quote-inject-tail=%% quote=headers
      reply
b5
!.
      set quote \\
         quote-inject-head='"\$'"'\\
            (%%%%a=%%a %%%%d=%%d %%%%f=%%f %%%%i=%%i %%%%n=%%n %%%%r=%%r)\\
            \\n'"'"' \\
         quote-inject-tail='"\$'"'\\
            (%%%%a=%%a %%%%d=%%d %%%%f=%%f %%%%i=%%i %%%%n=%%n %%%%r=%%r)\\
            \\n'"'"'
      reply
b6
!.
      set showname datefield=%%y nodatefield-markout-older indentprefix=\\ :
      reply
b7
!.
   ' | ${MAILX} ${ARGS} -Smta=test://"$MBOX" -Rf \
         -Sescape=! -Sindentprefix=' >' \
         ./.tmbox >./.tall 2>&1
   #}}}
   check_ex0 18-estat
   ${cat} ./.tall >> "${MBOX}"
   check 18 - "${MBOX}" '385267528 3926'

   # quote-as-attachment, fullnames
   </dev/null ${MAILX} ${ARGS} -Rf \
         -Sescape=! \
         -S quote-as-attachment \
         -Y reply -Y yb1 -Y !. \
         -Y 'unset quote-as-attachment' \
         -Y 'reply;yb2' -Y !. \
         -Y 'set quote-as-attachment fullnames' \
         -Y ';reply;yb3' -Y !. \
         ./.tmbox >./.tall 2>&1
   check 19 0 ./.tall '2774517283 2571'

   # Moreover, quoting of several parts with all*
   t__gen_mimemsg from 'ex1@am.ple' subject for-repl > ./.tmbox
   check 20 0 ./.tmbox '1874764424 668'

   </dev/null ${MAILX} ${ARGS} -Rf \
         -Sescape=! -Sindentprefix=' |' \
         -Y 'set quote=allheaders' \
         -Y reply -Y !. \
         -Y 'set quote=allbodies' \
         -Y reply -Y !. \
         -Y xit \
         ./.tmbox >./.tall 2>&1
   check_ex0 21-estat
   have_feat filter-html-tagsoup && ck='946925637 1105' || ck='3587432511 1165'
   check 21 - ./.tall "${ck}"

   t_epilog "${@}"
} # }}}

t_forward() { # {{{
   t_prolog "${@}"

   t__gen_msg subject fwd1 body origb1 from 1 to 2 > "${MBOX}"
   t__gen_msg subject fwd2 body origb2 from 1 to 1 >> "${MBOX}"

   #{{{ Base (does not test "recipient record")
   t_it() {
      </dev/null ${MAILX} ${ARGS} -Rf \
         -Y ${1}' . "du <ex1@am.ple>"
b1
~.
      echo 1:$?/$^ERRNAME; echoerr 1:done
      set fullnames escape=!
      '${1}' 1 "du <ex2@am.ple>"
b2 fullnames
!.
      echo 2:$?/$^ERRNAME; echoerr 2:done
      # Some errors
      set nofullnames ea=$expandaddr expandaddr=-all
      '${1}' ` "du <ex3@am.ple>"
b3
!.
      echo 3:$?/$^ERRNAME; echoerr 3:done
      set expandaddr=$ea
      '${1}' ` ex4-nono@am.ple ex4@am.ple # the first is a non-match msglist
b4
!.
      echo 4:$?/$^ERRNAME; echoerr 4:done
      '${1}' # TODO not yet possible b5 !.
      echo 5:$?/$^ERRNAME; echoerr 5:done
      set expandaddr=$ea
      '${1}' 1 2 ex6@am.ple
b6-1
!.
b6-2
!.
      echo 6:$?/$^ERRNAME; echoerr 6:done
      set forward-add-cc fullnames
      '${1}' . ex7@am.ple
b7
!.
      echo 7:$?/$^ERRNAME; echoerr 7:done
      set nofullnames
      '${1}' . ex8@am.ple
b8
!.
      echo 8:$?/$^ERRNAME; echoerr 8:done
      #' \
         "${MBOX}" > ./.tall 2>./.terr
      return ${?}
   }
   #}}}

   t_it forward
   check 1 0 ./.tall '2356713156 2219'
   if have_feat uistrings && have_feat docstrings; then
      check 2 - ./.terr '3273108824 335'
   else
      t_echoskip '2:[!UISTRINGS]'
   fi

   t_it Forward
   check 3 0 ./.tall '2356713156 2219'
   if have_feat uistrings && have_feat docstrings; then
      check 4 - ./.terr '447176534 355'
   else
      t_echoskip '4:[!UISTRINGS]'
   fi
   ${rm} -f ex*

   #{{{ *record*, *outfolder* (reuses $MBOX)
   ${mkdir} .tfolder

   t_it() {
      </dev/null ${MAILX} ${ARGS} -Rf -Sescape=! -Sfolder=`${pwd}`/.tfolder \
      -Y '#
      '${1}' 1 ex1@am.ple
b1
!.
      echo 1:$?/$^ERRNAME; set record=.trec'${2}'; '${1}' 1 ex2@am.ple
b2
!.
      echo 2:$?/$^ERRNAME; set outfolder norecord; '${1}' 2 ex1@am.ple
b3
!.
      echo 3:$?/$^ERRNAME; set record=.trec'${2}'; '${1}' 2 ex2@am.ple
b4
!.
      echo 4:$?/$^ERRNAME
      #' \
         "${MBOX}" > ./.tall 2>&1
      check ${2} 0 ./.tall '3180366037 1212'
      if [ ${#} -ne 4 ]; then
         check ${3}-1 - ./.trec${2} '1769129556 304'
         check ${3}-2 - ./.tfolder/.trec${2} '2335391111 284'
      else
         [ -f ./.trec${2} ]; check_exn0 ${3}
         echo * > ./.tlst
         check ${3}-1 - ./.tlst '2020171298 8'
         check ${3}-2 - ./ex1 '1512529673 304'
         check ${3}-3 - ./ex2 '1769129556 304'
         [ -f ./.tfolder/.trec${2} ]; check_exn0 ${3}-4
         ( cd .tfolder && echo * > ./.tlst )
         check ${3}-5 - ./.tfolder/.tlst '2020171298 8'
         check ${3}-6 - ./.tfolder/ex1 '2016773910 284'
         check ${3}-7 - ./.tfolder/ex2 '2335391111 284'
      fi
   }
   #}}}

   t_it forward 5 6
   t_it Forward 7 8 yes
   #${rm} -f ex*

   #{{{ Injections, headerpick selection
   ${rm} -f "${MBOX}"
   t__x2_msg > ./.tmbox

   printf '#
      set quote=noheading forward-inject-head
      forward 1 ex1@am.ple
b1
!.
      headerpick forward retain cc from subject to
      forward 1 ex1@am.ple
b2
!.
      unheaderpick forward retain *
      forward 1 ex1@am.ple
b3
!.
      headerpick forward ignore in-reply-to reply-to message-id status
      set forward-inject-head=%% forward-inject-tail=%%
      forward 1 ex1@am.ple
b4
!.
      set forward-inject-head='"\$'"'\\
            (%%%%a=%%a %%%%d=%%d %%%%f=%%f %%%%i=%%i %%%%n=%%n %%%%r=%%r)\\
            \\n'"'"' \\
         forward-inject-tail='"\$'"'\\
            (%%%%a=%%a %%%%d=%%d %%%%f=%%f %%%%i=%%i %%%%n=%%n %%%%r=%%r)\\
            \\n'"'"'
      forward 1 ex1@am.ple
b5
!.
      set showname datefield=%%y nodatefield-markout-older
      forward 1 ex1@am.ple
b6
!.
   ' | ${MAILX} ${ARGS} -Smta=test://"$MBOX" -Rf \
         -Sescape=! \
         ./.tmbox >./.tall 2>&1
   #}}}
   check_ex0 9-estat
   ${cat} ./.tall >> "${MBOX}"
   check 9 - "${MBOX}" '2976943913 2916'

   # forward-as-attachment
   </dev/null ${MAILX} ${ARGS} -Rf \
         -Sescape=! \
         -S forward-inject-head=.head. \
         -S forward-inject-tail=.tail. \
         -S forward-as-attachment \
         -Y 'headerpick forward retain subject to from' \
         -Y 'forward ex1@am.ple' -Y b1 -Y !. \
         -Y 'unset forward-as-attachment' \
         -Y 'forward ex1@am.ple;b2' -Y !. \
         ./.tmbox >./.tall 2>&1
   check 10 0 ./.tall '799103633 1250'

   t_epilog "${@}"
} # }}}

t_resend() { # {{{
   t_prolog "${@}"

   t__gen_msg subject fwd1 body origb1 from 1 to 2 > "${MBOX}"
   t__gen_msg subject fwd2 body origb2 from 1 to 1 >> "${MBOX}"

   #{{{ Base
   t_it() {
      </dev/null ${MAILX} ${ARGS} -Rf \
         -Y ${1}' . "du <ex1@am.ple>"
      echo 1:$?/$^ERRNAME; echoerr 1:done
      set fullnames escape=!
      '${1}' 1 "du , da <ex2@am.ple>"
      echo 2:$?/$^ERRNAME; echoerr 2:done
      # Some errors
      set nofullnames ea=$expandaddr expandaddr=-all
      '${1}' ` "du <ex3@am.ple>"
      echo 3:$?/$^ERRNAME; echoerr 3:done
      set expandaddr=$ea
      '${1}' ` ex4-nono@am.ple ex4@am.ple # the first is a non-match msglist
      echo 4:$?/$^ERRNAME; echoerr 4:done
      '${1}' # TODO not yet possible b5 !.
      echo 5:$?/$^ERRNAME; echoerr 5:done
      set expandaddr=$ea
      '${1}' 1 2 ex6@am.ple
      echo 6:$?/$^ERRNAME; echoerr 6:done
      #' \
         "${MBOX}" > ./.tall 2>./.terr
      return ${?}
   }
   #}}}

   t_it resend
   check 1 0 ./.tall '1461006932 1305'
   if have_feat uistrings; then
      check 2 - ./.terr '138360532 210'
   else
      t_echoskip '2:[!UISTRINGS]'
   fi

   t_it Resend
   check 3 0 ./.tall '3674535444 958'
   if have_feat uistrings; then
      check 4 - ./.terr '138360532 210'
   else
      t_echoskip '4:[!UISTRINGS]'
   fi

   #{{{ *record*, *outfolder* (reuses $MBOX)
   ${mkdir} .tfolder

   t_it() {
      </dev/null ${MAILX} ${ARGS} -Rf -Sescape=! -Sfolder=`${pwd}`/.tfolder \
      -Y '#
      set record=.trec'${2}'; '${1}' 1 ex1@am.ple
      echo 1:$?/$^ERRNAME; set record-resent; '${1}' 1 ex2@am.ple
      echo 2:$?/$^ERRNAME; set outfolder norecord-resent; '${1}' 2 ex1@am.ple
      echo 3:$?/$^ERRNAME; set record-resent; '${1}' 2 ex2@am.ple
      echo 4:$?/$^ERRNAME
      #' \
         "${MBOX}" > ./.tall 2>&1
      check_ex0 ${2}
      if [ ${#} -ne 3 ]; then
         check ${2} - ./.tall '1711347390 992'
         check ${3}-1 - ./.trec${2} '2840978700 249'
         check ${3}-2 - ./.tfolder/.trec${2} '3219997964 229'
      else
         check ${2} - ./.tall '1391418931 724'
         check ${3}-1 - ./.trec${2} '473817710 182'
         check ${3}-2 - ./.tfolder/.trec${2} '2174632404 162'
      fi
   }
   #}}}

   t_it resend 5 6 yes
   t_it Resend 7 8

   t_epilog "${@}"
} # }}}
# }}}

# VFS {{{ TODO uses $MBOX etc, does not t1,t2,t3 etc.
t_copy() { # {{{
   t_prolog "${@}"

   t__gen_msg subject Copy1 from 1 to 1 body 'Body1' > "${MBOX}"
   t__gen_msg subject Copy2 from 1 to 1 body 'Body2' >> "${MBOX}"
   check 1 - "${MBOX}" '137107341 324' # for flag test

   #{{{
   </dev/null ${MAILX} ${ARGS} -f \
      -Y '#
   headers
   copy 10 .tf1
   echo 0:$?/$^ERRNAME
   headers
   copy .tf1
   echo 1:$?/$^ERRNAME
   headers
   copy .tf1 # no auto-advance
   echo 2:$?/$^ERRNAME
   headers
   copy 2 .tf2
   echo 3:$?/$^ERRNAME
   headers
   copy 1 2 .tf3
   echo 4:$?/$^ERRNAME
   headers
   !'"${chmod}"' 0444 .tf3
   copy 1 2 .tf3
   echo 5:$?/$^ERRNAME
   #' \
      "${MBOX}" > ./.tallx 2>./.terr
   #}}}
   check_ex0 2

   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   if [ -n "${HONOURS_READONLY}" ]; then
      n2_1=2-1 cs2_1='1913702840 1121'
      n2_4=2-4 cs2_4='3642131968 344'
      n2_5=2-5 cs2_5='2617612897 112'
   else
      n2_1=2-1-nrdonly cs2_1='1962556153 1146'
      n2_4=2-4-nrdonly cs2_4='3733058190 688'
      n2_5=2-5-nrdonly cs2_5='3989834342 80'
   fi
   check ${n2_1} - ./.tall "${cs2_1}"
   check 2-2 - ./.tf1 '686654461 334'
   check 2-3 - ./.tf2 '1931512953 162'
   check ${n2_4} - ./.tf3 "${cs2_4}"
   if have_feat uistrings; then
      check ${n2_5} - ./.terr "${cs2_5}"
   else
      t_echoskip '2-5:[!UISTRINGS]'
   fi

   ##
   check 3 - "${MBOX}" '1477662071 346'

   #{{{
   t_it() {
      t__gen_msg subject Copy1 from 1 to 1 body 'Body1' > "${MBOX}"
      t__gen_msg subject Copy2 from 1 to 1 body 'Body2' >> "${MBOX}"
      t__gen_msg subject Copy3 from ex@am.ple to 1 body 'Body3' >> "${MBOX}"
      check ${1} - "${MBOX}" '2667292819 473' # for flag test

      </dev/null ${MAILX} ${ARGS} -f \
         -Y "${3}"'
      '"${2}"'
      Copy
      echo 1:$?/$^ERRNAME
      '"${2}"'
      Copy
      echo 2:$?/$^ERRNAME
      '"${2}"'
      Copy 2
      echo 3:$?/$^ERRNAME
      '"${2}"'
      Copy 3
      echo 4:$?/$^ERRNAME
      '"${2}"'
      Copy *
      echo 5:$?/$^ERRNAME
      '"${2}"'
      #' \
         "${MBOX}" > ./.tallx 2>&1
      return ${?}
   }
   #}}}

   t_it 5 headers '#'
   check_ex0 5-1
   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   echo * > ./.tlst
   check 5-2 - ./.tlst '1058655452 9'
   check 5-3 - ./.tall '1543702808 1617'
   check 5-4 - ./from1 '1031912635 999'
   check 5-5 - ./ex '2400630246 149'
   ${rm} -f ./.tlst ./.tall ./from1 ./ex

   ${mkdir} .tfolder
   t_it 6 '#' 'set outfolder folder='"`${pwd}`"'/.tfolder'
   check_ex0 6-1
   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   echo * .tfolder/* > ./.tlst
   check 6-2 - ./.tlst '1865898363 29'
   ${cat} ./.tall >> ${ERR} #check 6-3 - ./.tall # TODO due to folder echoes
   check 6-4 - .tfolder/from1 '1031912635 999'
   check 6-5 - .tfolder/ex '2400630246 149'

   #{{{
   t__x2_msg > ./.tmbox

   t_it() {
      printf '#
         '"${1}"'
         echo 1:$?/$^ERRNAME
         headerpick save retain cc date from subject to
         '"${1}"'
         echo 2:$?/$^ERRNAME
         unheaderpick save retain *
         '"${1}"'
         echo 3:$?/$^ERRNAME
         headerpick save ignore status in-reply-to
         '"${1}"'
         echo 4:$?/$^ERRNAME
      #' | ${MAILX} ${ARGS} -Rf ./.tmbox > ./.tall 2>&1
      return ${?}
   }
   #}}}

   t_it 'copy ./.tout'
   check_ex0 7-estat
   check 7-1 - ./.tall '3805176908 152'
   check 7-2 - ./.tout '2447734879 1316'

   t_it Copy
   check_ex0 8-estat
   echo * > ./.tlst
   check 8-1 - ./.tall '1044700686 136'
   check 8-2 - ./mr2 '2447734879 1316'
   check 8-3 - ./.tlst '3190056903 4'

   t_epilog "${@}"
} # }}}

t_save() { # {{{
   t_prolog "${@}"

   t__gen_msg subject Save1 from 1 to 1 body 'Body1' > "${MBOX}"
   t__gen_msg subject Save2 from 1 to 1 body 'Body2' >> "${MBOX}"
   check 1 - "${MBOX}" '3634443864 324' # for flag test

   #{{{
   </dev/null ${MAILX} ${ARGS} -f \
      -Y '#
   headers
   save 10 .tf1
   echo 0:$?/$^ERRNAME
   headers
   save .tf1
   echo 1:$?/$^ERRNAME
   headers
   save .tf1 # no auto-advance
   echo 2:$?/$^ERRNAME
   headers
   save 2 .tf2
   echo 3:$?/$^ERRNAME
   headers
   save 1 2 .tf3
   echo 4:$?/$^ERRNAME
   headers
   !'"${chmod}"' 0444 .tf3
   save 1 2 .tf3
   echo 5:$?/$^ERRNAME
   #' \
      "${MBOX}" > ./.tallx 2>./.terr
   #}}}
   check_ex0 2

   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   if [ -n "${HONOURS_READONLY}" ]; then
      n2_1=2-1 cs2_1='2335843514 1121'
      n2_4=2-4 cs2_4='970407001 344'
      n2_5=2-5 cs2_5='45116475 112'
   else
      n2_1=2-1-nrdonly cs2_1='1736244784 1146'
      n2_4=2-4-nrdonly cs2_4='3903872811 688'
      n2_5=2-5-nrdonly cs2_5='720724138 80'
   fi
   check ${n2_1} - ./.tall "${cs2_1}"
   check 2-2 - ./.tf1 '2435434321 334'
   check 2-3 - ./.tf2 '920652966 162'
   check ${n2_4} - ./.tf3 "${cs2_4}"
   if have_feat uistrings; then
      check ${n2_5} - ./.terr "${cs2_5}"
   else
      t_echoskip '2-5:[!UISTRINGS]'
   fi

   ##
   check 3 - "${MBOX}" '1219692400 346'

   #{{{
   t_it() {
      t__gen_msg subject Save1 from 1 to 1 body 'Body1' > "${MBOX}"
      t__gen_msg subject Save2 from 1 to 1 body 'Body2' >> "${MBOX}"
      t__gen_msg subject Save3 from ex@am.ple to 1 body 'Body3' >> "${MBOX}"
      check ${1} - "${MBOX}" '1391391227 473' # for flag test

      </dev/null ${MAILX} ${ARGS} -f \
         -Y "${3}"'
      '"${2}"'
      Save
      echo 1:$?/$^ERRNAME
      '"${2}"'
      Save
      echo 2:$?/$^ERRNAME
      '"${2}"'
      Save 2
      echo 3:$?/$^ERRNAME
      '"${2}"'
      Save 3
      echo 4:$?/$^ERRNAME
      '"${2}"'
      Save *
      echo 5:$?/$^ERRNAME
      '"${2}"'
      #' \
         "${MBOX}" > ./.tallx 2>&1
      return ${?}
   }
   #}}}

   t_it 5 headers '#'
   check_ex0 5-1
   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   echo * > ./.tlst
   check 5-2 - ./.tlst '1058655452 9'
   check 5-3 - ./.tall '3418590770 1617'
   check 5-4 - ./from1 '1462882526 999'
   check 5-5 - ./ex '2153575326 149'
   ${rm} -f ./.tlst ./.tall ./from1 ./ex

   ${mkdir} .tfolder
   t_it 6 '#' 'set outfolder folder='"`${pwd}`"'/.tfolder'
   check_ex0 6-1
   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   echo * .tfolder/* > ./.tlst
   check 6-2 - ./.tlst '1865898363 29'
   ${cat} ./.tall >> ${ERR} #check 6-3 - ./.tall # TODO due to folder echoes
   check 6-4 - .tfolder/from1 '1462882526 999'
   check 6-5 - .tfolder/ex '2153575326 149'

   #{{{
   t_it() {
      t__x2_msg > ./.tmbox
      check ${1} - ./.tmbox '561523988 397'

      a='-Rf'
      [ ${#} -gt 2 ] && a='-S MBOX=./.tmboxx'
      [ ${#} -gt 3 ] && a="${a}"' -S inbox=./.tmbox'
      printf '#
         headers
         '"${2}"'
         echo 1:$?/$^ERRNAME
         headers
         headerpick save retain cc date from subject to
         '"${2}"'
         echo 2:$?/$^ERRNAME
         unheaderpick save retain *
         '"${2}"'
         echo 3:$?/$^ERRNAME
         headerpick save ignore status in-reply-to
         '"${2}"'
         echo 4:$?/$^ERRNAME
      #' | ${MAILX} ${ARGS} -f ${a} ./.tmbox > ./.tall 2>&1
      return ${?}
   }
   #}}}

   t_it 7 'save ./.tout'
   check_ex0 7-estat
   check 7-1 - ./.tall '4190949581 312'
   check 7-2 - ./.tout '2447734879 1316'
   check 7-3 - ./.tmbox '561523988 397'

   t_it 8 Save
   check_ex0 8-estat
   echo * > ./.tlst
   check 8-1 - ./.tall '2109832180 296'
   check 8-2 - ./mr2 '2447734879 1316'
   check 8-3 - ./.tlst '3190056903 4'
   check 8-3 - ./.tmbox '561523988 397'

   # saves in $MBOX without argument
   t_it 9 save yes
   check_ex0 9-estat
   check 9-1 - ./.tall '652005824 320'
   check 9-2 - ./.tmboxx '2447734879 1316'
   check 9-3 - ./.tmbox '561523988 397'

   # and deletes if editing a primary mailbox
   ${rm} -f ./.tmboxx
   t_it 10 save yes yes
   check_ex0 10-estat
   check 10-1 - ./.tall '652005824 320'
   check 10-2 - ./.tmboxx '2447734879 1316'
   [ -f ./.tmbox ]; check_exn0 10-3

   t_epilog "${@}"
} # }}}

t_move() { # {{{
   t_prolog "${@}"

   t__gen_msg subject Move1 from 1 to 1 body 'Body1' > "${MBOX}"
   t__gen_msg subject Move2 from 1 to 1 body 'Body2' >> "${MBOX}"
   check 1 - "${MBOX}" '2967134193 324' # for flag test

   #{{{
   </dev/null ${MAILX} ${ARGS} -f \
      -Y '#
   headers
   move 10 .tf1
   echo 0:$?/$^ERRNAME
   headers
   move .tf1
   echo 1:$?/$^ERRNAME
   headers
   !touch .tf2; '"${chmod}"' 0444 .tf2
   move 2 .tf2
   echo 2:$?/$^ERRNAME
   !'"${chmod}"' 0644 .tf2
   move 2 .tf2
   echo 3:$?/$^ERRNAME
   headers
   #' \
      "${MBOX}" > ./.tallx 2>./.terr
   #}}}
   check_ex0 2

   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   if [ -n "${HONOURS_READONLY}" ]; then
      n2_1=2-1 cs2_1='1641443074 491'
      n2_4=2-4 cs2_4='602144474 155'
   else
      n2_1=2-1-nrdonly cs2_1='3045412111 492'
      n2_4=2-4-nrdonly cs2_4='2197157669 201'
   fi
   check ${n2_1} - ./.tall "${cs2_1}"
   check 2-2 - ./.tf1 '1473857906 162'
   check 2-3 - ./.tf2 '331229810 162'
   if have_feat uistrings; then
      check ${n2_4} - ./.terr "${cs2_4}"
   else
      t_echoskip '2-4:[!UISTRINGS]'
   fi

   ##
   check 3 - "${MBOX}" '4294967295 0'

   #{{{
   t_it() {
      t__gen_msg subject Move1 from 1 to 1 body 'Body1' > "${MBOX}"
      t__gen_msg subject Move2 from 1 to 1 body 'Body2' >> "${MBOX}"
      t__gen_msg subject Move3 from ex@am.ple to 1 body 'Body3' >> "${MBOX}"
      check ${1} - "${MBOX}" '2826896131 473' # for flag test

      </dev/null ${MAILX} ${ARGS} -f \
         -Y "${3}"'
      '"${2}"'
      Move
      echo 1:$?/$^ERRNAME
      '"${2}"'
      Move 2
      echo 2:$?/$^ERRNAME
      '"${2}"'
      Move 3
      echo 3:$?/$^ERRNAME
      '"${2}"'
      undelete *
      echo 4:$?/$^ERRNAME
      '"${2}"'
      Move *
      echo 5:$?/$^ERRNAME
      '"${2}"'
      #' \
         "${MBOX}" > ./.tallx 2>./.terr
      return ${?}
   }
   #}}}

   t_it 5 headers '#'
   check_ex0 5-1
   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   echo * > ./.tlst
   check 5-2 - ./.tlst '1058655452 9'
   check 5-3 - ./.tall '419037676 870'
   if have_feat uistrings; then
      check 5-4 - ./.terr '1383646464 86'
   else
      t_echoskip '5-4:[!UISTRINGS]'
   fi
   check 5-5 - ./from1 '3719268580 827'
   check 5-6 - ./ex '4262925856 149'
   ${rm} -f ./.tlst ./.tall ./.terr ./from1 ./ex

   ${mkdir} .tfolder
   t_it 6 '#' 'set outfolder folder='"`${pwd}`"'/.tfolder'
   check_ex0 6-1
   if have_feat uistrings; then # TODO
      ${sed} -e '$bP' -e d -e :P < ./.tallx >> "${ERR}"
      ${sed} '$d' < ./.tallx > ./.tall
   else
      ${mv} ./.tallx ./.tall
   fi
   echo * .tfolder/* > ./.tlst
   check 6-2 - ./.tlst '1865898363 29'
   ${cat} ./.tall >> ${ERR} #check 6-3 - ./.tall # TODO due to folder echoes
   check 6-4 - .tfolder/from1 '3719268580 827'
   check 6-5 - .tfolder/ex '4262925856 149'

   #{{{
   t__x2_msg > ./.tmbox

   t_it() {
      printf '#
         '"${1}"'
         echo 1:$?/$^ERRNAME
         headerpick save retain cc date from subject to
         '"${1}"'
         echo 2:$?/$^ERRNAME
         unheaderpick save retain *
         '"${1}"'
         echo 3:$?/$^ERRNAME
         headerpick save ignore status in-reply-to
         '"${1}"'
         echo 4:$?/$^ERRNAME
      #' | ${MAILX} ${ARGS} -Rf ./.tmbox > ./.tall 2>&1
      return ${?}
   }
   #}}}

   t_it 'move ./.tout'
   check_ex0 7-estat
   check 7-1 - ./.tall '3805176908 152'
   check 7-2 - ./.tout '2447734879 1316'

   t_it Move
   check_ex0 8-estat
   echo * > ./.tlst
   check 8-1 - ./.tall '1044700686 136'
   check 8-2 - ./mr2 '2447734879 1316'
   check 8-3 - ./.tlst '3190056903 4'

   t_epilog "${@}"
} # }}}

t_mbox() { # {{{
   t_prolog "${@}"

   (
      i=1
      while [ ${i} -lt 113 ]; do
         printf 'm file://%s\n~s Subject %s\nHello %s!\n~.\n' \
            "${MBOX}" "${i}" "${i}"
         i=`add ${i} 1`
      done
   ) | ${MAILX} ${ARGS} > .tall 2>&1
   check 1 0 "${MBOX}" '1785801373 13336'
   check 1-outerr - ./.tall '4294967295 0' # empty file

   printf 'File "%s"\ncopy * "%s"\nFile "%s"\nfrom*' \
         "${MBOX}" .tmbox1 .tmbox1 |
      ${MAILX} ${ARGS} -Sshowlast > .tall 2>&1
   check 2 0 .tall '3467540956 8991'

   printf 'File "%s"\ncopy * "file://%s"\nFile "file://%s"\nfrom*' \
      "${MBOX}" .tmbox2 .tmbox2 | ${MAILX} ${ARGS} -Sshowlast > .tall 2>&1
   check 3 0 .tall '2410946529 8998'

   # copy only the odd (but the first), move the even
   (
      printf 'File "file://%s"\ncopy ' .tmbox2
      i=1
      while [ ${i} -lt 113 ]; do
         printf '%s ' "${i}"
         i=`add ${i} 2`
      done
      printf 'file://%s\nFile "file://%s"\nfrom*' .tmbox3 .tmbox3
   ) | ${MAILX} ${ARGS} -Sshowlast > .tall 2>&1
   check 4 0 .tmbox3 '2554734733 6666'
   check 5 - .tall '2062382804 4517'
   # ...
   (
      printf 'file "file://%s"\nmove ' .tmbox2
      i=2
      while [ ${i} -lt 113 ]; do
         printf '%s ' "${i}"
         i=`add ${i} 2`
      done
      printf 'file://%s\nFile "file://%s"\nfrom*\nFile "file://%s"\nfrom*' \
         .tmbox3 .tmbox3 .tmbox2
   ) | ${MAILX} ${ARGS} -Sshowlast > .tall 2>>${ERR}
   check 6 0 .tmbox3 '1429216753 13336'
   if have_feat uistrings; then
      ${sed} 2d < .tall > .tallx
   else
      ${cp} .tall .tallx
   fi
   check 7 - .tallx '169518319 13477'

   # Invalid MBOXes (after [f4db93b3])
   echo > .tinvmbox
   printf 'copy 1 ./.tinvmbox' | ${MAILX} ${ARGS} -Rf "${MBOX}" > .tall 2>&1
   check 8 0 .tinvmbox '2848412822 118'
   check 9 - ./.tall '461280182 33'

   echo ' ' > .tinvmbox
   printf 'copy 1 ./.tinvmbox' | ${MAILX} ${ARGS} -Rf "${MBOX}" > .tall 2>&1
   check 10 0 .tinvmbox '624770486 120'
   check 11 - ./.tall '461280182 33'

   { echo; echo; } > .tinvmbox # (not invalid)
   printf 'copy 1 ./.tinvmbox' | ${MAILX} ${ARGS} -Rf "${MBOX}" > .tall 2>&1
   check 12 0 .tinvmbox '1485640875 119'
   check 13 - ./.tall '461280182 33'

   # *mbox-rfc4155*, plus
   ${cat} <<-_EOT > ./.tinv1
		 
		
		From MAILER-DAEMON-1 Wed Oct  2 01:50:07 1996
		Date: Wed, 02 Oct 1996 01:50:07 +0000
		To:
		Subject: Bad bad message 1
		
		From me to you, blinde Kuh!
		
		From MAILER-DAEMON-2 Wed Oct  2 01:50:07 1996
		Date: Wed, 02 Oct 1996 01:50:07 +0000
		To:
		Subject: Bad bad message 2
		
		From me to you, blindes Kalb!
		_EOT
   ${cp} ./.tinv1 ./.tinv2

   printf \
      'define mboxfix {
         \\local set mbox-rfc4155; \\File "${1}"; \\eval copy * "${2}"
      }
      call mboxfix ./.tinv1 ./.tok' | ${MAILX} ${ARGS} > .tall 2>&1
   check_ex0 14-estat
   ${cat} ./.tinv1 ./.tok >> .tall
   check 14 - ./.tall '739301109 616'

   printf \
      'file ./.tinv1 # ^From not repaired, but missing trailing NL is
      File ./.tok # Just move away to nowhere
      set mbox-rfc4155
      file ./.tinv2 # Fully repaired
      File ./.tok' | ${MAILX} ${ARGS} >>${ERR} 2>&1
   check_ex0 15-estat
   # Almost EQ since [Auto-fix when MBOX had From_ errors on read (Dr. Werner
   # Fink).]
   check 15-1 - ./.tinv1 '4026377396 312'
   check 15-2 - ./.tinv2 '4151504442 314'

   # *mbox-fcc-and-pcc*
   ${cat} > ./.ttmpl <<-'_EOT'
	Fcc: ./.tfcc1
	Bcc: | cat >> ./.tpcc1
	Fcc:        ./.tfcc2           
	Subject: fcc and pcc, and *mbox-fcc-and-pcc*
	
	one line body
	_EOT

   < ./.ttmpl ${MAILX} ${ARGS} -t > "${MBOX}" 2>&1
   check 16 0 "${MBOX}" '4294967295 0'
   check 17 - ./.tfcc1 '2301294938 148'
   check 18 - ./.tfcc2 '2301294938 148'
   check 19 - ./.tpcc1 '2301294938 148'

   < ./.ttmpl ${MAILX} ${ARGS} -t -Snombox-fcc-and-pcc > "${MBOX}" 2>&1
   check 20 0 "${MBOX}" '4294967295 0'
   check 21 - ./.tfcc1 '3629108107 98'
   check 22 - ./.tfcc2 '3629108107 98'
   check 23 - ./.tpcc1 '2373220256 246'

   # More invalid: since in "copy * X" messages will be copied in `sort' order,
   # reordering may happen, and before ([f5db11fe] (a_cwrite_save1(): FIX:
   # ensure pre-v15 MBOX separation "in between" messages.., 2019-08-07) that
   # could still have created invalid MBOX files!
   #{{{
   ${cat} <<-_EOT > ./.tinv1
		 
		
		From MAILER-DAEMON-4 Sun Oct  4 01:50:07 1998
		Date: Sun, 04 Oct 1998 01:50:07 +0000
		Subject: h4
		
		B4
		
		From MAILER-DAEMON-0 Fri Oct 28 21:02:21 2147483649
		Date: Nix, 01 Oct BAD 01:50:07 +0000
		Subject: hinvalid
		
		BINV
		
		From MAILER-DAEMON-3 Fri Oct  3 01:50:07 1997
		Date: Fri, 03 Oct 1997 01:50:07 +0000
		Subject: h3
		
		B3
		
		From MAILER-DAEMON-1 Sun Oct  1 01:50:07 1995
		Date: Sun, 01 Oct 1995 01:50:07 +0000
		Subject:h1
		
		B1
		
		
		From MAILER-DAEMON-2 Wed Oct  2 01:50:07 1996
		Date: Wed, 02 Oct 1996 01:50:07 +0000
		Subject: h2
		
		b2
		_EOT
   printf \
      'File ./.tinv1
      sort date
      remove ./.tinv2
      copy * ./.tinv2
      file ./.tinv1' | ${MAILX} ${ARGS} >>${ERR} 2>&1
   #}}}
   check 24 0 ./.tinv1 '104184185 560'
   check 25 - ./.tinv2 '853754737 510'

   #{{{ More corner cases
   ${cat} <<-'_EOT' > ./t26.mbox
	Leading text, what to do with it?

	From MAILER-DAEMON-nono-0 Wed Oct  2 01:50:07 1996

	This is a body, but not a valid message

	From MAILER-DAEMON-2 Wed Oct  2 01:50:07 1996
	ToMakeItHappen: header

	From MAILER-DAEMON-nono-1 Wed Oct  2 01:50:07 1996

	This is a body, but not a valid message, 2

	From MAILER-DAEMON-4 Wed Oct  2 01:50:07 1996
	One: header

	From MAILER-DAEMON-nono-2 Wed Oct  2 01:50:07 1996

	From MAILER-DAEMON-nono-3 Wed Oct  2 01:50:07 1996

	From MAILER-DAEMON-nono-4 Wed Oct  2 01:50:07 1996

	And do foolish things

	From MAILER-DAEMON-6 Wed Oct  2 01:50:07 1996
	One: two
	three
	_EOT
   #}}}
   ${MAILX} ${ARGS} -Rf \
      -Y 'headers;echo 1;Show 1;echo 2;Show 2;echo 3;Show 3;echo 4' \
      -Y 'copy * ./t27' \
      -Y 'copy 1 ./t28' \
      ./t26.mbox > ./t26 2>&1
   check 26 0 ./t26 '312000791 3259'
   check 27 - ./t27 '3764405655 487'
   check 28 - ./t28 '2228574283 184'

   t_epilog "${@}"
} # }}}

t_maildir() { # {{{
   t_prolog "${@}"

   if have_feat maildir; then :; else
      t_echoskip '[!MAILDIR]'
      t_epilog "${@}"
      return
   fi

   (
      i=0
      while [ ${i} -lt 112 ]; do
         printf 'm file://%s\n~s Subject %s\nHello %s!\n~.\n' \
            "${MBOX}" "${i}" "${i}"
         i=`add ${i} 1`
      done
   ) | ${MAILX} ${ARGS}
   check 1 0 "${MBOX}" '2366902811 13332'

   printf 'File "%s"
         copy * "%s"
         File "%s"
         from*
      ' "${MBOX}" .tmdir1 .tmdir1 |
      ${MAILX} ${ARGS} -Snewfolders=maildir -Sshowlast > .tlst
   check 2 0 .tlst '3442251309 8991'

   printf 'File "%s"
         copy * "maildir://%s"
         File "maildir://%s"
         from*
      ' "${MBOX}" .tmdir2 .tmdir2 |
      ${MAILX} ${ARGS} -Sshowlast > .tlst
   check 3 0 .tlst '3524806062 9001'

   printf 'File "maildir://%s"
         copy * "file://%s"
         File "file://%s"
         from*
      ' .tmdir2 .tmbox1 .tmbox1 |
      ${MAILX} ${ARGS} -Sshowlast > .tlst
   check 4 0 .tmbox1 '4096198846 12772'
   check 5 - .tlst '1262452287 8998'

   # only the odd (even)
   (
      printf 'File "maildir://%s"
            copy ' .tmdir2
      i=0
      while [ ${i} -lt 112 ]; do
         j=`modulo ${i} 2`
         [ ${j} -eq 1 ] && printf '%s ' "${i}"
         i=`add ${i} 1`
      done
      printf ' file://%s
            File "file://%s"
            from*
         ' .tmbox2 .tmbox2
   ) | ${MAILX} ${ARGS} -Sshowlast > .tlst
   check 6 0 .tmbox2 '4228337024 6386'
   check 7 - .tlst '2078821439 4517'
   # ...
   (
      printf 'file "maildir://%s"
            move ' .tmdir2
      i=0
      while [ ${i} -lt 112 ]; do
         j=`modulo ${i} 2`
         [ ${j} -eq 0 ] && [ ${i} -ne 0 ] && printf '%s ' "${i}"
         i=`add ${i} 1`
      done
      printf ' file://%s
            File "file://%s"
            from*
            File "maildir://%s"
            from*
         ' .tmbox2 .tmbox2 .tmdir2
   ) | ${MAILX} ${ARGS} -Sshowlast > .tlst
   check 8 0 .tmbox2 '978751761 12656'
   ${sed} 2d < .tlst > .tlstx
   check 9 - .tlstx '2172297531 13477'

   # More invalid: since in "copy * X" messages will be copied in `sort' order,
   # reordering may happen, and before ([f5db11fe] (a_cwrite_save1(): FIX:
   # ensure pre-v15 MBOX separation "in between" messages.., 2019-08-07) that
   # could still have created invalid MBOX files!
   #{{{
   ${cat} <<-_EOT > ./.tinv1
		 
		
		From MAILER-DAEMON-4 Sun Oct  4 01:50:07 1998
		Date: Sun, 04 Oct 1998 01:50:07 +0000
		Subject: h4
		
		B4
		
		From MAILER-DAEMON-0 Fri Oct 28 21:02:21 2147483649
		Date: Nix, 01 Oct BAD 01:50:07 +0000
		Subject: hinvalid
		
		BINV
		
		From MAILER-DAEMON-3 Fri Oct  3 01:50:07 1997
		Date: Fri, 03 Oct 1997 01:50:07 +0000
		Subject: h3
		
		B3
		
		From MAILER-DAEMON-1 Sun Oct  1 01:50:07 1995
		Date: Sun, 01 Oct 1995 01:50:07 +0000
		Subject:h1
		
		B1
		
		
		From MAILER-DAEMON-2 Wed Oct  2 01:50:07 1996
		Date: Wed, 02 Oct 1996 01:50:07 +0000
		Subject: h2
		
		b2
		_EOT
   #}}}
   printf \
      'File ./.tinv1
      sort date
      copy * maildir://./.tmdir10
      !{ for f in ./.tmdir10/new/*; do echo ===; %s $f; done; } > ./.t11
      File ./.tmdir10
      sort date
      copy * ./.t10warp
   ' "${cat}" | ${MAILX} ${ARGS} >>${ERR} 2>&1
   # Note that substdate() fixes all but one From_ line to $SOURCE_DATE_EPOCH!
   check 10 0 ./.t10warp '3551111321 502'
   check 11 - ./.t11 '642719592 302'

   #
   ${mkdir} .z .z/cur .z/new .z/tmp
   printf '' > .z/new/844221007.M13P13108.reproducible_build:2,s
   printf '\n' > .z/new/844221007.M12P13108.reproducible_build:2,s
   printf 'a\n' > .z/new/844221007.M11P13108.reproducible_build:2,s
	printf 'From MAILER-DAEMON-0 Sat Apr 24 21:54:00 2021\n\nb\n' \
      > .z/new/844221007.M10P13108.reproducible_build:2,s
   </dev/null ${MAILX} ${ARGS} -Rf \
      -Y 'h;echo 1;p 1;echo 2;p2;echo 3;p3;echo 4;p4' \
      -Y 'copy * maildir://./.z2' \
      -Y 'File ./.z2' \
      -Y 'h;echo 1;p 1;echo 2;p2;echo 3;p3;echo 4;p4' \
      ./.z > t12 2>>${ERR}
   check 12 0 ./t12 '3263166940 1569'

   t_epilog "${@}"
} # }}}

t_eml_and_stdin_pipe() { # {{{
   t_prolog "${@}"

   t__gen_msg from pipe-committee subject stdin > ./t.mbox
   ${sed} 1d < ./t.mbox > ./t.eml

   check 1 0 ./t.mbox '1038043487 130'
   check 2 0 ./t.eml '2467547934 81'

   #
   <./t.mbox ${MAILX} ${ARGS} -Y 'p;x' -Serrexit -Rf - >./t3 2>&1
   check 3 0 ./t3 '3232332927 182'

   <./t.mbox ${MAILX} ${ARGS} -Y 'p;x' -Serrexit -Rf mbox://- >./t4 2>&1
   check 4 0 ./t4 '3232332927 182'

   ${cat} ./t.mbox | ${MAILX} ${ARGS} -Y 'p;x' -Serrexit -Rf - >./t5 2>&1
   check 5 0 ./t5 '3232332927 182'

   ${cat} ./t.mbox | ${MAILX} ${ARGS} -Y 'p;x' -Serrexit \
      -Rf mbox://- >./t6 2>&1
   check 6 0 ./t6 '3232332927 182'

   #
   <./t.eml ${MAILX} ${ARGS} -Y 'p;x' -Serrexit -Rf - >./t7 2>&1
   check 7 0 ./t7 '3085605104 210'

   <./t.eml ${MAILX} ${ARGS} -Y 'p;x' -Serrexit -Rf eml://- >./t8 2>&1
   check 8 0 ./t8 '269911796 177'

   ${cat} ./t.eml | ${MAILX} ${ARGS} -Y 'p;x' -Serrexit -Rf - >./t9 2>&1
   check 9 0 ./t9 '3085605104 210'

   ${cat} ./t.eml | ${MAILX} ${ARGS} -Y 'p;x' -Serrexit -Rf eml://- >./t10 2>&1
   check 10 0 ./t10 '269911796 177'

   #
   <./t.mbox ${MAILX} ${ARGS} -f - >./t11 2>&1
   check 11 1 ./t11 '2465941229 88'

   <./t.eml ${MAILX} ${ARGS} -f eml://- >./t12 2>&1
   check 12 1 ./t12 '3267665338 77'

   # The big nothing
   echo A | ${MAILX} ${ARGS} -Y 'p;x' -Rf eml://- >./t13 2>&1
   check 13 0 ./t13 '1897903061 97'

   echo | ${MAILX} ${ARGS} -Y 'p;x' -Rf eml://- >./t14 2>&1
   check 14 0 ./t14 '1336432318 96'

   </dev/null ${MAILX} ${ARGS} -Y 'p;x' -Rf eml://- >./t15 2>&1
   check 15 0 ./t15 '2143650081 96'

   t_epilog "${@}"
} # }}}

# }}}

# MIME and RFC basics {{{
t_mime_if_not_ascii() { # {{{
   t_prolog "${@}"

   </dev/null ${MAILX} ${ARGS} -s Subject ./t.mbox >> ./t.mbox 2>&1
   check 1 0 ./t.mbox '3647956381 106'

   </dev/null ${MAILX} ${ARGS} -Scharset-7bit=not-ascii -s Subject ./t.mbox \
      >> ./t.mbox 2>&1
   check 2 0 ./t.mbox '3964303752 274'

   t_epilog "${@}"
} # }}}

t_mime_encoding() { # {{{
   t_prolog "${@}"

   # 8B
   printf 'Hey, you.\nFrom me to you\nCiao\n' |
      ${MAILX} ${ARGS} -s Subject -Smime-encoding=8b ./t.mbox \
         >> ./t.mbox 2>&1
   check 1 0 ./t.mbox '3835153597 136'

   printf 'Hey, you.\n\nFrom me to you\nCiao.\n' |
      ${MAILX} ${ARGS} -s Subject -Smime-encoding=8b ./t.mbox \
         >> ./t.mbox 2>&1
   check 2 0 ./t.mbox '63875210 275'

   # QP
   printf 'Hey, you.\n From me to you\nCiao\n' |
      ${MAILX} ${ARGS} -s Subject -Smime-encoding=qp ./t.mbox \
         >> ./t.mbox 2>&1
   check 3 0 ./t.mbox '465798521 412'

   printf 'Hey, you.\nFrom me to you\nCiao\n' |
      ${MAILX} ${ARGS} -s Subject -Smime-encoding=qp ./t.mbox \
         >> ./t.mbox 2>&1
   check 4 0 ./t.mbox '2075263697 655'

   # B64
   printf 'Hey, you.\n From me to you\nCiao\n' |
      ${MAILX} ${ARGS} -s Subject -Smime-encoding=b64 ./t.mbox \
         >> ./t.mbox 2>&1
   check 5 0 ./t.mbox '601672771 792'

   printf 'Hey, you.\nFrom me to you\nCiao\n' |
      ${MAILX} ${ARGS} -s Subject -Smime-encoding=b64 ./t.mbox \
         >> ./t.mbox 2>&1
   check 6 0 ./t.mbox '3926760595 1034'

   t_epilog "${@}"
} # }}}

t_xxxheads_rfc2047() { # {{{
   t_prolog "${@}"

   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s 'a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲' \
      ./t1
   check 1 0 ./t1 '3422562347 371'

   # Single word (overlong line split -- bad standard! Requires injection of
   # artificial data!!  But can be prevented by using RFC 2047 encoding)
   i=`${awk} 'BEGIN{for(i=0; i<92; ++i) printf "0123456789_"}'`
   echo | ${MAILX} ${ARGS} -s "${i}" ./t2
   check 2 0 ./t2 '3317256266 1714'

   # Combination of encoded words, space and tabs of varying sort
   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s "1Abrä Kaspas1 2Abra Katä	b_kaspas2  \
3Abrä Kaspas3   4Abrä Kaspas4    5Abrä Kaspas5     \
6Abra Kaspas6      7Abrä Kaspas7       8Abra Kaspas8        \
9Abra Kaspastäb4-3 	 	 	 10Abra Kaspas1 _ 11Abra Katäb1	\
12Abra Kadabrä1 After	Tab	after	Täb	this	is	NUTS" \
      ./t3
   check 3 0 ./t3 '786672837 587'

   # Overlong multibyte sequence that must be forcefully split
   # todo This works even before v15.0, but only by accident
   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s "✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄" \
      ./t4
   check 4 0 ./t4 '2889557767 655'

   # Trailing WS
   echo | ${MAILX} ${ARGS} \
      -s "1-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-5 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-6 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      ./t5
   check 5 0 ./t5 '3135161683 293'

   # Leading and trailing WS
   echo | ${MAILX} ${ARGS} \
      -s "	 	 2-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      ./t6
   check 6 0 ./t6 '3221845405 232'

   # RFC 2047 in an address field!  (Missing test caused v14.9.6!)
   echo "Dat Früchtchen riecht häußlich" |
      ${MAILX} ${ARGS} ${ADDARG_UNI} -Sfullnames -Smta=test://./t7 \
         -s Hühöttchen \
         'Schnödes "Früchtchen" <do@du> (Hä!)'
   check 7 0 ./t7 '3681801246 373'

   # RFC 2047 in an address field, and iconv involved
   if have_feat iconv; then
      ${cat} > ./t8-in <<'_EOT'
From zaza@exam.ple  Fri Mar  2 21:31:56 2018
Date: Fri, 2 Mar 2018 20:31:45 +0000
From: z=?iso-8859-1?Q?=E1?=za <zaza@exam.ple>
To: dude <dude@exam.ple>
Subject: houston(...)
Message-ID: <abra@1>
MIME-Version: 1.0
Content-Type: text/plain; charset=iso-8859-1
Content-Disposition: inline
Content-Transfer-Encoding: 8bit

_EOT
      echo reply | ${MAILX} ${ARGS} ${ADDARG_UNI} \
         -Sfullnames -Sreply-in-same-charset \
         -Smta=test://t8 -Rf ./t8-in
      check 8 0 ./t8 '3499372945 285'
   else
      t_echoskip '8:[!ICONV]'
   fi

   # TODO This test is false: mx_mime_display_from_header() rewrite!!!
   ${cat} > ./t9-in <<'_EOT'
From zaza@exam.ple  Mon Mar 21 20:49:17 2022
Date: Mon, 21 Mar 2022 20:49:17 +0100
From: za=?us-ascii?Q?=?=za <zaza@exam.ple>,
 za=??Q?=?=za <zaz2a@exam.ple>,
 za=???=?=za <zaz3a@exam.ple>,
 za=?us-ascii??=?=za <zaz4a@exam.ple>,
To: x <a@b.ple>
Subject: c
Message-ID: <a@1>
MIME-Version: 1.0
Content-Type: text/plain; charset=us-ascii
Content-Disposition: inline
Content-Transfer-Encoding: 8bit

_EOT
   echo type | ${MAILX} ${ARGS} \
      -Rf ./t9-in > ./t9 2>>${ERR}
   check 9 0 ./t9 '1718457763 424'

   t_epilog "${@}"
} # }}}

t_iconv_mbyte_base64() { # {{{ TODO uses sed(1) and special *headline*!!
   t_prolog "${@}"

   if [ -n "${UTF8_LOCALE}" ] && have_feat multibyte-charsets &&
         have_feat iconv; then
      if (</dev/null iconv -f ascii -t iso-2022-jp) >/dev/null 2>&1 ||
            (</dev/null iconv -f ascii -t euc-jp) >/dev/null 2>&1; then
         :
      else
         t_echoskip '[ICONV/iconv(1):missing conversion(s)]'
         t_epilog "${@}"
         return
      fi
   else
      t_echoskip '[no UTF-8 locale or !MULTIBYTE-CHARSETS or !ICONV]'
      t_epilog "${@}"
      return
   fi

   if (</dev/null iconv -f ascii -t iso-2022-jp) >/dev/null 2>&1; then
      ${cat} <<-'_EOT' | LC_ALL=${UTF8_LOCALE} ${MAILX} ${ARGS} \
            -Smta=test://t1_to_4 \
            -Sescape=! -Smime-encoding=base64 2>./t2
         set ttycharset=utf-8 sendcharsets=iso-2022-jp
         m t1@exam.ple
!s Japanese from UTF-8 to ISO-2022-JP
シジュウカラ科（シジュウカラか、学名 Paridae）は、鳥類スズメ目の科である。シジュウカラ（四十雀）と総称されるが、狭義にはこの1種をシジュウカラと呼ぶ。

カンムリガラ（学名Parus cristatus）は、スズメ目シジュウカラ科に分類される鳥類の一種。


カンムリガラ（学名Parus cristatus）は、スズメ目シジュウカラ科に分類される鳥類の一種。

シジュウカラ科（シジュウカラか、学名 Paridae）は、鳥類スズメ目の科である。シジュウカラ（四十雀）と総称されるが、狭義にはこの1種をシジュウカラと呼ぶ。
!.

         set ttycharset=iso-2022-jp charset-7bit=iso-2022-jp sendcharsets=utf-8
         m t2@exam.ple
!s Japanese from ISO-2022-JP to UTF-8, eh, no, also ISO-2022-JP
$B%7%8%e%&%+%i2J!J%7%8%e%&%+%i$+!"3XL>(B Paridae$B!K$O!"D;N`%9%:%aL\$N2J$G$"$k!#%7%8%e%&%+%i!J;M==?}!K$HAm>N$5$l$k$,!"695A$K$O$3$N(B1$B<o$r%7%8%e%&%+%i$H8F$V!#(B

$B%+%s%`%j%,%i!J3XL>(BParus cristatus$B!K$O!"%9%:%aL\%7%8%e%&%+%i2J$KJ,N`$5$l$kD;N`$N0l<o!#(B


$B%+%s%`%j%,%i!J3XL>(BParus cristatus$B!K$O!"%9%:%aL\%7%8%e%&%+%i2J$KJ,N`$5$l$kD;N`$N0l<o!#(B

$B%7%8%e%&%+%i2J!J%7%8%e%&%+%i$+!"3XL>(B Paridae$B!K$O!"D;N`%9%:%aL\$N2J$G$"$k!#%7%8%e%&%+%i!J;M==?}!K$HAm>N$5$l$k$,!"695A$K$O$3$N(B1$B<o$r%7%8%e%&%+%i$H8F$V!#(B
!.
		_EOT
      # May not presume iconv output as long as roundtrip possible [489a7122]
      check_ex0 1-estat
      ${awk} 'BEGIN{h=1}/^$/{++h;next}{if(h % 2 == 1)print}' \
         < ./t1_to_4 > ./t1
      check 1 - ./t1 '3314001564 516'
      check 2 - ./t2 '4294967295 0'

      printf 'eval f 1; eval write ./t3; eval type 1; eval type 2\n' |
         LC_ALL=${UTF8_LOCALE} ${MAILX} ${ARGS} \
            -S headline="%>%a%m %-18f %-16d %i%-s" \
            -Rf ./t1_to_4 >./t4 2>&1
      check 3 0 ./t3 '1259742080 686'
      # TODO check 4 - ./t4 '3214068822 2123'
         ${sed} -e '/^\[-- M/d' < ./t4 > ./t4-x
         check 4 - ./t4-x '4042322737 2025'
   else
      t_echoskip '1-4:[ICONV/iconv(1):ISO-2022-JP unsupported]'
   fi

   if (</dev/null iconv -f ascii -t euc-jp) >/dev/null 2>&1; then
      ${cat} <<-'_EOT' | LC_ALL=${UTF8_LOCALE} ${MAILX} ${ARGS} \
            -Smta=test://t5_to_8 \
            -Sescape=! -Smime-encoding=base64 2>./t6
         set ttycharset=utf-8 sendcharsets=euc-jp
         m t1@exam.ple
!s Japanese from UTF-8 to EUC-JP
シジュウカラ科（シジュウカラか、学名 Paridae）は、鳥類スズメ目の科である。シジュウカラ（四十雀）と総称されるが、狭義にはこの1種をシジュウカラと呼ぶ。

カンムリガラ（学名Parus cristatus）は、スズメ目シジュウカラ科に分類される鳥類の一種。


カンムリガラ（学名Parus cristatus）は、スズメ目シジュウカラ科に分類される鳥類の一種。

シジュウカラ科（シジュウカラか、学名 Paridae）は、鳥類スズメ目の科である。シジュウカラ（四十雀）と総称されるが、狭義にはこの1種をシジュウカラと呼ぶ。
!.

         set ttycharset=EUC-JP sendcharsets=utf-8
         m t2@exam.ple
!s Japanese from EUC-JP to UTF-8
�����奦����ʡʥ����奦���餫����̾ Paridae�ˤϡ�Ļ�ॹ�����ܤβʤǤ��롣�����奦����ʻͽ����ˤ����Τ���뤬�������ˤϤ���1��򥷥��奦����ȸƤ֡�

�����ꥬ��ʳ�̾Parus cristatus�ˤϡ��������ܥ����奦����ʤ�ʬ�व���Ļ��ΰ�


�����ꥬ��ʳ�̾Parus cristatus�ˤϡ��������ܥ����奦����ʤ�ʬ�व���Ļ��ΰ�

�����奦����ʡʥ����奦���餫����̾ Paridae�ˤϡ�Ļ�ॹ�����ܤβʤǤ��롣�����奦����ʻͽ����ˤ����Τ���뤬�������ˤϤ���1��򥷥��奦����ȸƤ֡�
!.
		_EOT
      check_ex0 5-estat
      ${awk} 'BEGIN{h=1}/^$/{++h;next}{if(h % 2 == 1)print}' \
         < t5_to_8 > ./t5
      check 5 - ./t5 '1754179361 469'
      check 6 - ./t6 '4294967295 0'

      printf 'eval f 1; eval write ./t7; eval type 1; eval type 2\n' |
         LC_ALL=${UTF8_LOCALE} ${MAILX} ${ARGS} \
            -S headline="%>%a%m %-18f %-16d %i%-s" \
            -Rf ./t5_to_8 >./t8 2>&1
      check 7 0 ./t7 '1259742080 686'
      #TODO check 8 - ./t8 '2506063395 2075'
         ${sed} -e '/^\[-- M/d' < ./t8 > ./t8-x
         check 8 - ./t8-x '504034186 1978'
   else
      t_echoskip '5-8:[ICONV/iconv(1):EUC-JP unsupported]'
   fi

   t_epilog "${@}"
} # }}}

t_iconv_mainbody() { # {{{
   t_prolog "${@}"

   if [ -n "${UTF8_LOCALE}" ] && have_feat iconv; then :; else
      t_echoskip '[no UTF-8 locale or !ICONV]'
      t_epilog "${@}"
      return
   fi

   printf '–' | ${MAILX} ${ARGS} ${ADDARG_UNI} -Smta=test://t.mbox \
      -S charset-7bit=us-ascii -S charset-8bit=utf-8 \
      -s '–' over-the@rain.bow 2>./t2
   check 1 0 ./t.mbox '3559538297 250'
   check 2 - ./t2 '4294967295 0'

   printf '–' | ${MAILX} ${ARGS} ${ADDARG_UNI} -Smta=test://t.mbox \
      -S charset-7bit=us-ascii -S charset-8bit=us-ascii \
      -s '–' over-the@rain.bow 2>./t4
   check_exn0 3
   check 3 - ./t.mbox '3559538297 250'
   if have_feat uistrings; then
      check 4 - ./t4 '271380835 121'
   else
      t_echoskip '4:[!UISTRINGS]'
   fi

   # The different iconv(3) implementations use different replacement sequence
   # types (character-wise, byte-wise, and the character(s) used differ)
   i="${MAILX_ICONV_MODE}"
   if [ -n "${i}" ]; then
      printf 'p\nx\n' | ${MAILX} ${ARGS} -Rf ./t.mbox >./t5-xxx 2>./t5-1
      j=${?}
      check_ex0 5-1-estat ${j}
      check 5-1 - ./t5-1 '4294967295 0'
      if [ ${i} -eq 13 ]; then
         check 5-2 - ./t5-xxx '189327996 283' # XXX old (before test MTA)
      elif [ ${i} -eq 12 ]; then
         check 5-3 - ./t5-xxx '1959197095 283' # XXX old (before test MTA)
      elif [ ${i} -eq 3 ]; then
         check 5-4 - ./t5-xxx '3544755786 278'
      else
         check 5-5 - ./t5-xxx '2381160335 278'
      fi
   else
      t_echoskip '5:[ICONV replacement unknown]'
   fi

   t_epilog "${@}"
} # }}}

t_binary_mainbody() { # {{{
   t_prolog "${@}"

   printf 'abra\0\nka\r\ndabra' |
      ${MAILX} ${ARGS} ${ADDARG_UNI} -s 'binary with carriage-return!' \
      ./t.mbox 2>./t2
   check 1 0 ./t.mbox '1629827 239'
   check 2 - ./t2 '4294967295 0'

   printf 'p\necho\necho writing now\nwrite ./t5\n' |
      ${MAILX} ${ARGS} -Rf \
         -Spipe-application/octet-stream="?* ${cat} > ./t4" \
         ./t.mbox >./t3 2>&1
   check 3 0 ./t3 '970629510 314'
   check 4 - ./t4 '3817108933 15'
   check 5 - ./t5 '3817108933 15'

   t_epilog "${@}"
} # }}}

t_mime_force_sendout() { # {{{
   t_prolog "${@}"

   if have_feat iconv; then :; else
      t_echoskip '[!ICONV]'
      t_epilog "${@}"
      return
   fi

   printf '\150\303\274' > ./.tmba
   printf 'ha' > ./.tsba
   printf '' > ./t.mbox

   printf '\150\303\244' | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -s nogo \
      over-the@rain.bow 2>>${ERR}
   check 1 4 ./t.mbox '4294967295 0'

   printf '\150\303\244' | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -s go -Smime-force-sendout \
      over-the@rain.bow 2>>${ERR}
   check 2 0 ./t.mbox '1866273282 219'

   printf ha | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -s nogo \
      -a ./.tmba over-the@rain.bow 2>>${ERR}
   check 3 4 ./t.mbox '1866273282 219'

   printf ha | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -s go -Smime-force-sendout \
      -a ./.tmba over-the@rain.bow 2>>${ERR}
   check 4 0 ./t.mbox '644433809 880'

   printf ha | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -s nogo \
      -a ./.tsba -a ./.tmba over-the@rain.bow 2>>${ERR}
   check 5 4 ./t.mbox '644433809 880'

   printf ha | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -s go -Smime-force-sendout \
      -a ./.tsba -a ./.tmba over-the@rain.bow 2>>${ERR}
   check 6 0 ./t.mbox '3172365123 1729'

   printf '\150\303\244' | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -s nogo \
      -a ./.tsba -a ./.tmba over-the@rain.bow 2>>${ERR}
   check 7 4 ./t.mbox '3172365123 1729'

   printf '\150\303\244' | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -s go -Smime-force-sendout \
      -a ./.tsba -a ./.tmba over-the@rain.bow 2>>${ERR}
   check 8 0 ./t.mbox '4002905306 2565'

   t_epilog "${@}"
} # }}}

t_C_opt_customhdr() { # {{{
   t_prolog "${@}"

   echo bla |
   ${MAILX} ${ARGS} -Smta=test://t1 \
      -C 'C-One  :  Custom One Body' \
      -C 'C-Two:CustomTwoBody' \
      -C 'C-Three:      CustomThreeBody   ' \
      -S customhdr='chdr1:  chdr1 body, chdr2:chdr2 body, chdr3: chdr3 body ' \
      this-goes@nowhere >./t1-x 2>&1
   check_ex0 1-estat
   ${cat} ./t1-x >> t1
   check 1 0 t1 '2535463301 238'

   printf 'm this-goes@nowhere\nbody\n!.
      unset customhdr
      m this-goes2@nowhere\nbody2\n!.
      set customhdr=%ccustom1 :  custom1  body%c
      m this-goes3@nowhere\nbody3\n!.
      set customhdr=%ccustom1 :  custom1\\,  body  ,  \\
            custom2: custom2  body ,  custom-3 : custom3 body ,\\
            custom-4:custom4-body     %c
      m this-goes4@nowhere\nbody4\n!.
   ' "'" "'" "'" "'" |
   ${MAILX} ${ARGS} -Smta=test://t2 -Sescape=! \
      -C 'C-One  :  Custom One Body' \
      -C 'C-Two:CustomTwoBody' \
      -C 'C-Three:                   CustomThreeBody  ' \
      -C '   C-Four:CustomFourBody  ' \
      -C 'C-Five:CustomFiveBody' \
      -S customhdr='ch1:  b1 , ch2:b2, ch3:b3 ,ch4:b4,  ch5: b5 ' \
      > ./t2-x 2>&1
   check_ex0 2-estat
   ${cat} ./t2-x >> ./t2
   check 2 0 ./t2 '544085062 1086'

   t_epilog "${@}"
}
# }}}
# }}}

# Operational basics with trivial tests {{{
t_alias() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -Smta=test://t1 > ./t2 2>&1
   alias a1 ex1@a1.ple
   alias a1 ex2@a1.ple "EX3 <ex3@a1.ple>"
   alias a1 ex4@a1.ple
   alias a2 ex1@a2.ple ex2@a2.ple ex3@a2.ple ex4@a2.ple
   alias a3 a4
   alias a4 a5 ex1@a4.ple
   alias a5 a6
   alias a6 a7 ex1@a6.ple
   alias a7 a8
   alias a8 ex1@a8.ple
   alias a1
   alias a2
   alias a3
   m a1
	~c a2
	~b a3
	~r - '_EOT'
   This body is!
   This also body is!!
_EOT
	__EOT
   #}}}
   check 1 0 ./t1 '139467786 277'
   check 2 - ./t2 '1598893942 133'

   if have_feat uistrings; then
      #{{{
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t3 2>&1
		commandalias x echo '$?/$^ERRNAME'
		echo 1
		alias a:bra!  ha@m beb@ra ha@m '' zeb@ra ha@m; x
		alias a:bra!; x
		alias ha@m  ham-expansion  ha@m '';x
		alias ha@m;x
		alias beb@ra  ceb@ra beb@ra1;x
		alias beb@ra;x
		alias ceb@ra  ceb@ra1;x
		alias ceb@ra;x
		alias deb@ris   '';x
		alias deb@ris;x
		echo 2
		alias - a:bra!;x
		alias - ha@m;x
		alias - beb@ra;x
		alias - ceb@ra;x
		alias - deb@ris;x
		echo 3
		unalias ha@m;x
		alias - a:bra!;x
		unalias beb@ra;x
		alias - a:bra!;x
		echo 4
		unalias*;x;alias;x
		echo 5
		\alias noexpa@and this@error1;x
		\alias ha@m '\noexp@and' expa@and \\noexp@and2;x
		\alias ha@m;x
		\alias - ha@m;x
		\alias noexpa@and2 this@error2;x
		\alias expa1@and this@error3;x
		\alias expa@and \\expa1@and;x
		\alias expa@and;x
		\alias - ha@m;x
		\alias - expa@and;x
		__EOT
      #}}}
      check 3 0 ./t3 '1513155156 796'
   else
      t_echoskip '3:[!UISTRINGS]'
   fi

   # TODO t_alias: n_ALIAS_MAXEXP is compile-time constant,
   # TODO need to somehow provide its contents to the test, then test

   t_epilog "${@}"
} # }}}

t_charsetalias() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
   commandalias x echo '$?/$^ERRNAME'
	echo 1
	charsetalias latin1 latin15;x
	charsetalias latin1;x
	charsetalias - latin1;x
	echo 2
	charsetalias cp1252 latin1  latin15 utf8  utf8 utf16;x
	charsetalias cp1252;x
	charsetalias latin15;x
	charsetalias utf8;x
	echo 3
	charsetalias - cp1252;x
	charsetalias - latin15;x
	charsetalias - utf8;x
	echo 4
	charsetalias latin1;x
	charsetalias - latin1;x
	uncharsetalias latin15;x
	charsetalias latin1;x
	charsetalias - latin1;x
	__EOT
   #}}}
   check 1 0 ./t1 '3551595280 433'

   t_epilog "${@}"
} # }}}

t_shortcut() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > ./t1 2>&1
   commandalias x echo '$?/$^ERRNAME'
	echo 1
	shortcut file1 expansion-of-file1;x
	shortcut file2 expansion-of-file2;x
	shortcut file3 expansion-of-file3;x
	shortcut   file4   'expansion of file4'  'file 5' 'expansion of file5';x
	echo 2
	shortcut file1;x
	shortcut file2;x
	shortcut file3;x
	shortcut file4;x
	shortcut 'file 5';x
	echo 3
	shortcut;x
	__EOT
   #}}}
   check 1 0 ./t1 '1970515669 430'

   t_epilog "${@}"
} # }}}

t_netrc() { # {{{
   t_prolog "${@}"

   if have_feat netrc; then :; else
      t_echoskip '[!NETRC]'
      t_epilog "${@}"
      return
   fi

   #{{{
   printf '# comment
      machine x.local login a1	machine x.local login a2 password p2
      machine	x.local	login	a3	password	"p 3"
      machine
      pop.x.local
      login
      a2
      password
      p2-pop!
      machine *.x.local login a2 password p2-any!
      machine y.local login ausr password apass
      machine
      z.local password
      noupa
      # and unused default
      default login defacc password defpass
   ' > ./.tnetrc
   ${chmod} 0600 ./.tnetrc
   #}}}

   printf 'netrc;echo =$?;netrc c;echo =$?;netr loa;echo =$?;netr s;echo =$?' |
      NETRC=./.tnetrc ${MAILX} ${ARGS} > ./t1 2>&1
   check 1 0 ./t1 '2911708535 542'

   #{{{
   have_feat uistrings && i='3076722625 893' || i='3808149439 645'
   printf '# Comment
      echo ==host
      netrc loo x.local
      netrc loo y.local
      netrc loo z.local
      echo ==(re)load cache
      netrc load;echo $?/$^ERRNAME
      echo ==usr@host
      netrc loo a1@x.local
      netrc loo a2@x.local
      netrc loo a3@x.local
      netrc loo a4@x.local
      echo ==clear cache
      netrc clear;echo $?/$^ERRNAME
      echo ==usr@x.host
      netrc loo a2@pop.x.local
      netrc loo a2@imap.x.local
      netrc loo a2@smtp.x.local
      echo ==usr@y.x.host
      netrc loo a2@nono.smtp.x.local
      echo ==[usr@]unknown-host
      netrc loo a.local
      netrc loo defacc@a.local
      netrc loo a1@a.local
   ' | NETRC=./.tnetrc ${MAILX} ${ARGS} > ./t2 2>&1
   #}}}
   check 2 0 ./t2 "${i}"

   t_epilog "${@}"
} # }}}
# }}}

# Operational basics with easy tests {{{
t_expandaddr() { # {{{
   # after: t_alias
   # MTA alias specific part in t_mta_aliases()
   # This only tests from command line, rest later on (iff any)
   t_prolog "${@}"

   if have_feat uistrings; then :; else
      t_echoskip '[!UISTRINGS]'
      t_epilog "${@}"
      return
   fi

   echo "${cat}" > ./t.cat
   ${chmod} 0755 ./t.cat

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat > ./t.pipe' talias taddr@exam.ple \
      > ./t2 2>&1
   check 1 4 ./t.mbox '1216011460 138'
   check 2 - ./t2 '3404105912 162'

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t4 2>&1
   check 3 0 ./t.mbox '847567042 276'
   check 4 - ./t4 '4294967295 0'
   check 5 - t.file '1216011460 138'
   check 6 - t.pipe '1216011460 138'

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,+file,+pipe,+name,+addr \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t8 2>&1
   check 7 0 ./t.mbox '3682360102 414'
   check 8 - ./t8 '4294967295 0'
   check 9 - t.file '847567042 276'
   check 10 - t.pipe '1216011460 138'

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,+file,-file,+pipe,+name,+addr \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t12 2>&1
   check 11 4 ./t.mbox '1010907786 552'
   check 12 - ./t12 '2897686659 70'
   check 13 - t.file '847567042 276'
   check 14 - t.pipe '1216011460 138'

   printf '' > ./t.pipe
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=fail,-all,+file,-file,+pipe,+name,+addr \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t16 2>&1
   check 15 4 ./t.mbox '1010907786 552'
   check 16 - ./t16 '50695798 179'
   check 17 - t.file '847567042 276'
   check 18 - t.pipe '4294967295 0'

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,+file,+pipe,-pipe,+name,+addr \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t20 2>&1
   check 19 4 ./t.mbox '3359494254 690'
   check 20 - ./t20 '1751120754 91'
   check 21 - t.file '3682360102 414'
   check 22 - t.pipe '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=fail,-all,+file,+pipe,-pipe,+name,+addr \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t24 2>&1
   check 23 4 ./t.mbox '3359494254 690'
   check 24 - ./t24 '4118644033 200'
   check 25 - t.file '3682360102 414'
   check 26 - t.pipe '4294967295 0'

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,+file,+pipe,+name,-name,+addr \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t28 2>&1
   check 27 0 ./t.mbox '3735108703 828'
   check 28 - ./t28 '4294967295 0'
   check 29 - t.file '1010907786 552'
   check 30 - t.pipe '1216011460 138'

   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,+file,+pipe,+name,-name,+addr \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t32 2>&1
   check 31 4 ./t.mbox '4225234603 949'
   check 32 - ./t32 '3486613973 73'
   check 33 - t.file '452731060 673'
   check 34 - t.pipe '1905076731 121'

   printf '' > ./t.pipe
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=fail,-all,+file,+pipe,+name,-name,+addr \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t36 2>&1
   check 35 4 ./t.mbox '4225234603 949'
   check 36 - ./t36 '3032065285 182'
   check 37 - t.file '452731060 673'
   check 38 - t.pipe '4294967295 0'

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,+file,+pipe,+name,+addr,-addr \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t40 2>&1
   check 39 4 ./t.mbox '4225234603 949'
   check 40 - ./t40 '3863610168 169'
   check 41 - t.file '1975297706 775'
   check 42 - t.pipe '130065764 102'

   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,+file,+pipe,+name,+addr,-addr \
      -Sadd-file-recipients \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t44 2>&1
   check 43 4 ./t.mbox '4225234603 949'
   check 44 - ./t44 '3863610168 169'
   check 45 - t.file '1210943515 911'
   check 46 - t.pipe '1831110400 136'

   printf '' > ./t.pipe
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=fail,-all,+file,+pipe,+name,+addr,-addr \
      -Sadd-file-recipients \
      -X'alias talias talias@exam.ple' \
      ./t.file '  |  ./t.cat >./t.pipe' talias taddr@exam.ple \
      > ./t48 2>&1
   check 47 4 ./t.mbox '4225234603 949'
   check 48 - ./t48 '851041772 278'
   check 49 - t.file '1210943515 911'
   check 50 - t.pipe '4294967295 0'

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,+addr \
      taddr@exam.ple this@@c.example \
      > ./t52 2>&1
   check 51 4 ./t.mbox '473729143 1070'
   check 52 - ./t52 '2646392129 66'

   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sexpandaddr=-all,failinvaddr \
      taddr@exam.ple this@@c.example \
      > ./t54 2>&1
   check 53 4 ./t.mbox '473729143 1070'
   check 54 - ./t54 '887391555 175'

   #
   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sthis=taddr@exam.ple -Sexpandaddr \
      -c '\$this' -b '\$this' '\$this' \
      > ./t56 2>&1
   check 55 4 ./t.mbox '473729143 1070'
   check 56 - ./t56 '3680176617 141'

   </dev/null ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -ssub \
      -Sthis=taddr@exam.ple -Sexpandaddr=shquote \
      -c '\$this' -b '\$this' '\$this' \
      > ./t58 2>&1
   check 57 0 ./t.mbox '398243793 1191'
   check 58 - ./t58 '4294967295 0'

   #
   printf '' > ./t.mbox
   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sadd-file-recipients \
         -Sexpandaddr=-all,+fcc \
         > ./t60 2>&1
	Fcc: t.file1
	Fcc: t.file2
	_EOT
   check 59 0 ./t.mbox '4294967295 0'
   check 60 - ./t60 '4294967295 0'
   check 61 - t.file1 '3178309482 124'
   check 62 - t.file2 '3178309482 124'

   printf '' > ./t.mbox
   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sadd-file-recipients \
         -Sexpandaddr=-all,+file \
         > ./t64 2>&1
	Fcc: t.file1
	Fcc: t.file2
	_EOT
   check 63 0 ./t.mbox '4294967295 0'
   check 64 - ./t64 '4294967295 0'
   check 65 - t.file1 '3027266938 248'
   check 66 - t.file2 '3027266938 248'

   printf '' > ./t.mbox
   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sadd-file-recipients \
         -Sexpandaddr=-all,+file,-fcc \
         > ./t68 2>&1
	Fcc: t.file1
	Fcc: t.file2
	_EOT
   check 67 0 ./t.mbox '4294967295 0'
   check 68 - ./t68 '4294967295 0'
   check 69 - t.file1 '1875215222 372'
   check 70 - t.file2 '1875215222 372'

   printf '' > ./t.mbox
   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sadd-file-recipients \
         -Sexpandaddr=-all,+fcc,-file \
         > ./t72 2>&1
	Fcc: t.file1
	Fcc: t.file2
	_EOT
   check 71 4 ./t.mbox '4294967295 0'
   check 72 - ./t72 '2936929607 223'
   check 73 - t.file1 '1875215222 372'
   check 74 - t.file2 '1875215222 372'

   printf '' > ./t.mbox
   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sadd-file-recipients \
         -Sexpandaddr=-all,fail,+addr \
         > ./t76 2>&1
	Fcc: t.file1
	Fcc: t.file2
	To: never@exam.ple
	_EOT
   check 75 4 ./t.mbox '4294967295 0'
   check 76 - ./t76 '4156837575 247'
   check 77 - t.file1 '1875215222 372'
   check 78 - t.file2 '1875215222 372'

   #
   printf '' > ./t.mbox
   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sexpandaddr=fail,domaincheck \
         > ./t80 2>&1
	To: one@localhost
	_EOT
   check 79 0 ./t.mbox '171635532 120'
   check 80 - ./t80 '4294967295 0'

   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sexpandaddr=domaincheck \
         > ./t82 2>&1
	To: one@localhost  ,    Hey two <two@exam.ple>, Trouble <three@tro.uble>
	_EOT
   check 81 4 ./t.mbox '2659464839 240'
   check 82 - ./t82 '1119895397 158'

   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sexpandaddr=fail,domaincheck \
         > ./t84 2>&1
	To: one@localhost  ,    Hey two <two@exam.ple>, Trouble <three@tro.uble>
	_EOT
   check 83 4 ./t.mbox '2659464839 240'
   check 84 - ./t84 '1577313789 267'

   ${cat} <<-_EOT |\
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t.mbox -t -ssub \
         -Sexpandaddr=fail,domaincheck \
         -Sexpandaddr-domaincheck=exam.ple,tro.uble \
         > ./t86 2>&1
	To: one@localhost  ,    Hey two <two@exam.ple>, Trouble <three@tro.uble>
	_EOT
   check 85 0 ./t.mbox '1670655701 410'
   check 86 - ./t86 '4294967295 0'

   #
	printf 'To: <reproducible_build>' |
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t87.mbox -t -ssub \
         -Sexpandaddr=nametoaddr \
         > ./t88 2>&1
   check 87 0 ./t87.mbox '1288059511 146'
   check 88 - ./t88 '4294967295 0'

	printf 'To: <reproducible_build>' |
      ${MAILX} ${ARGS} -Snoexpandaddr -Smta=test://t89.mbox -t -ssub \
         -Sexpandaddr=nametoaddr -Shostname=nowhere \
         > ./t90 2>&1
   check 89 0 ./t89.mbox '3724074854 203'
   check 90 - ./t90 '4294967295 0'

   t_epilog "${@}"
} # }}}

t_mta_aliases() { # {{{
   # after: t_expandaddr
   t_prolog "${@}"

   if have_feat mta-aliases; then :; else
      t_echoskip '[!MTA_ALIASES]'
      t_epilog "${@}"
      return
   fi

   #{{{
   ${cat} > ./t.ali <<- '__EOT'
	
	   # Comment
	
	
	a1: ex1@a1.ple  , 
	  ex2@a1.ple, <ex3@a1.ple> ,
	  ex4@a1.ple    
	a2:     ex1@a2.ple  ,   ex2@a2.ple,a2_2
	a2_2:ex3@a2.ple,ex4@a2.ple
	a3: a4
	a4: a5,
	# Comment
	      # More comment
	   ex1@a4.ple
	# Comment
	a5: a6
	a6: a7  , ex1@a6.ple
	a7: a8,a9
	a8: ex1@a8.ple
	__EOT
   #}}}
   </dev/null ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -Smta-aliases=./t.ali \
      -X mtaaliases \
      -X xit > ./tlist 2>&1
   check list 0 ./tlist '1644126449 193'

   </dev/null ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -Smta-aliases=./t.ali \
      -Y 'def y {
         x a1;x a2;x a2_2;x a3;x a4;x a5;x a6;x a7;x a8;x a9;x a10
      }
      \commandali x \\mtaali -
      \call y
      \commandali x \\mtaali
      \call y
      ' > ./tlook 2>./tlook-err
   check look 0 ./tlook '3425855815 506'
   if have_feat uistrings; then
      check look-err - ./tlook-err '2628536915 170'
   else
      t_echoskip 'look-err:[!UISTRINGS]'
   fi

   echo | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -Smta-aliases=./t.ali \
      -b a3 -c a2 a1 > ./t2 2>&1
   check 1 0 ./t.mbox '1172368381 238'
   check 2 - ./t2 '4294967295 0'

   ## xxx The following are actually *expandaddr* tests!!

   # May not send plain names over SMTP!
   mtaali=
   if have_feat smtp; then
      echo | ${MAILX} ${ARGS} \
         -Smta=smtp://laber.backe -Ssmtp-config=-ehlo \
         -Smta-aliases=./t.ali \
         -b a3 -c a2 a1 > ./t5 2>&1
      check_exn0 3
      check 4 - ./t.mbox '1172368381 238'
      mtaali=1
   fi
   if [ -n "${mtaali}" ] && have_feat uistrings; then
      check 5 - ./t5 '771616226 179'
   else
      t_echoskip '5:[!SMTP/!UISTRINGS]'
   fi

   # xxx for false-positive SMTP test we would need some mocking
   echo | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -Sexpandaddr=fail,-name \
      -Smta-aliases=./t.ali \
      -b a3 -c a2 a1 > ./t8 2>&1
   check_exn0 6
   check 7 - ./t.mbox '1172368381 238'
   if have_feat uistrings; then
      check 8 - ./t8 '2834389894 178'
   else
      t_echoskip '8:[!UISTRINGS]'
   fi

   echo | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -Sexpandaddr=-name \
      -Smta-aliases=./t.ali \
      -b a3 -c a2 a1 > ./t10 2>&1
   check 9 4 ./t.mbox '2322273994 472'
   if have_feat uistrings; then
      check 10 - ./t10 '2136559508 69'
   else
      t_echoskip '10:[!UISTRINGS]'
   fi

   echo 'a9:nine@nine.nine' >> ./t.ali

   echo | ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -Sexpandaddr=fail,-name \
      -Smta-aliases=./t.ali \
      -b a3 -c a2 a1 > ./t12 2>&1
   check 11 0 ./t.mbox '2422268299 722'
   check 12 - ./t12 '4294967295 0'

   #{{{
   printf '#
   set expandaddr=-name
   mail a1
!c a2
!:echo $?/$^ERRNAME
!^header insert bcc a3
!:echo $?/$^ERRNAME
!:set expandaddr
!t a1
!c a2
!:echo $?/$^ERRNAME
!^header insert bcc a3
!:echo $?/$^ERRNAME
!.
   echo and, once again, check that cache is updated
   # Enclose one pipe in quotes: immense stress for our stupid address parser:(
   !echo "a10:./t.f1,|%s>./t.p1,\\"|%s > ./t.p2\\",./t.f2" >> ./t.ali
   mtaaliases load
   mail a1
!c a2
!:echo $?/$^ERRNAME
!^header insert bcc a3
!:echo $?/$^ERRNAME
!.
   echo trigger happiness
   mail a1
!c a2
!:echo $?/$^ERRNAME
!^header insert bcc "a3 a10"
!:echo $?/$^ERRNAME
!.
   ' "${cat}" "${cat}" | ${MAILX} ${ARGS} -Smta=test://t.mbox -Sescape=! \
      -Smta-aliases=./t.ali \
      > ./t14 2>&1
   #}}}
   check 13 0 ./t.mbox '550955032 1469'
   if have_feat uistrings; then
      check 14 - ./t14 '1795496020 473'
   else
      t_echoskip '14:[!UISTRINGS]'
   fi
   check 15 - ./t.f1 '3056269950 249'
   check 16 - ./t.p1 '3056269950 249'
   check 17 - ./t.p2 '3056269950 249'
   check 18 - ./t.f2 '3056269950 249'

   # TODO t_mta_aliases: n_ALIAS_MAXEXP is compile-time constant,
   # TODO need to somehow provide its contents to the test, then test

   t_epilog "${@}"
} # }}}

t_filetype() { # {{{
   t_prolog "${@}"

   printf 'm m1@e.t\nL1\nHy1\n~.\nm m2@e.t\nL2\nHy2\n~@ %s\n~.\n' \
      "${TOPDIR}snailmail.jpg" | ${MAILX} ${ARGS} -Smta=test://t.mbox
   check 1 0 ./t.mbox '1314354444 13536'

   if (echo | gzip -c) >/dev/null 2>&1; then
      {
         printf 'File "%s"\ncopy 1 ./t2.mbox.gz\ncopy 2 ./t2.mbox.gz' \
            ./t.mbox | ${MAILX} ${ARGS} -X'filetype gz gzip\ -dc gzip\ -c'
         printf 'File ./t2.mbox.gz\ncopy * ./t2.mbox\n' |
            ${MAILX} ${ARGS} -X'filetype gz gzip\ -dc gzip\ -c'
      } > ./t3 2>&1
      check 2 - ./t2.mbox '1314354444 13536'
      check 3 - ./t3 '1817728413 103'
   else
      t_echoskip '2-3:[missing gzip(1)]'
   fi

   {
      printf 'File "%s"\ncopy 1 ./t4.mbox.gz
            copy 2 ./t4.mbox.gz
            copy 1 ./t4.mbox.gz
            copy 2 ./t4.mbox.gz
            ' ./t.mbox |
         ${MAILX} ${ARGS} \
            -X'filetype gz gzip\ -dc gzip\ -c' \
            -X'filetype mbox.gz "${sed} 1,3d|${cat}" \
            "echo eins;echo zwei;echo und mit ${sed} bist Du dabei;${cat}"'
      printf 'File ./t4.mbox.gz\ncopy * ./t4.mbox\n' |
         ${MAILX} ${ARGS} \
            -X'filetype gz gzip\ -dc gzip\ -c' \
            -X'filetype mbox.gz "${sed} 1,3d|${cat}" kill\ 0'
   } > ./t5 2>&1
   check 4 - ./t4.mbox '2687765142 27092'
   check 5 - ./t5 '3154959458 173'

   t_epilog "${@}"
} # }}}

t_e_H_L_opts() { # {{{
   t_prolog "${@}"

   touch ./t.mbox
   ${MAILX} ${ARGS} -ef ./t.mbox
   echo ${?} > ./t1

   printf 'm me@exam.ple\nLine 1.\nHello.\n~.\n' |
   ${MAILX} ${ARGS} -Smta=test://t.mbox
   printf 'm you@exam.ple\nLine 1.\nBye.\n~.\n' |
   ${MAILX} ${ARGS} -Smta=test://t.mbox

   ${MAILX} ${ARGS} -ef ./t.mbox 2>> ./t1
   echo ${?} >> t1
   ${MAILX} ${ARGS} -efL @t@me ./t.mbox 2>> ./t1
   echo ${?} >> t1
   ${MAILX} ${ARGS} -efL @t@you ./t.mbox 2>> ./t1
   echo ${?} >> t1
   ${MAILX} ${ARGS} -efL '@>@Line 1' ./t.mbox 2>> ./t1
   echo ${?} >> t1
   ${MAILX} ${ARGS} -efL '@>@Hello.' ./t.mbox 2>> ./t1
   echo ${?} >> t1
   ${MAILX} ${ARGS} -efL '@>@Bye.' ./t.mbox 2>> ./t1
   echo ${?} >> t1
   ${MAILX} ${ARGS} -efL '@>@Good bye.' ./t.mbox 2>> ./t1
   echo ${?} >> t1

   ${MAILX} ${ARGS} -fH ./t.mbox >> ./t1 2>&1
   echo ${?} >> ./t1
   ${MAILX} ${ARGS} -fL @t@me ./t.mbox >> ./t1 2>&1
   echo ${?} >> ./t1
   ${MAILX} ${ARGS} -fL @t@you ./t.mbox >> ./t1 2>&1
   echo ${?} >> ./t1
   ${MAILX} ${ARGS} -fL '@>@Line 1' ./t.mbox >> ./t1 2>&1
   echo ${?} >> ./t1
   ${MAILX} ${ARGS} -fL '@>@Hello.' ./t.mbox >> ./t1 2>&1
   echo ${?} >> ./t1
   ${MAILX} ${ARGS} -fL '@>@Bye.' ./t.mbox >> ./t1 2>&1
   echo ${?} >> ./t1
   ${MAILX} ${ARGS} -fL '@>@Good bye.' ./t.mbox >> ./t1 2>>${ERR}
   echo ${?} >> ./t1

   check 1 - ./t1 '1369201287 670'

   ##

   printf 'm me1@exam.ple\n~s subject cab\nLine 1.\n~.\n' |
   ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -r '' -X 'set from=pony1@$LOGNAME'
   printf 'm me2@exam.ple\n~s subject bac\nLine 12.\n~.\n' |
   ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -r '' -X 'set from=pony2@$LOGNAME'
   printf 'm me3@exam.ple\n~s subject abc\nLine 123.\n~.\n' |
   ${MAILX} ${ARGS} -Smta=test://t.mbox \
      -r '' -X 'set from=pony3@$LOGNAME'

   ${MAILX} ${ARGS} -S on-mailbox-open=fh-test -X 'define fh-test {
         echo fh-test size; set autosort=size showname showto
      }' -fH ./t.mbox > ./t2 2>&1
   check 2 0 ./t2 '3132089442 413'

   ${MAILX} ${ARGS} -S on-mailbox-open=fh-test -X 'define fh-test {
         echo fh-test subject; set autosort=subject showname showto
      }' -fH ./t.mbox > ./t3 2>&1
   check 3 0 ./t3 '2813644801 416'

   ${MAILX} ${ARGS} -S on-mailbox-open=fh-test -X 'define fh-test {
         echo fh-test from; set autosort=from showto
      }' -fH ./t.mbox > ./t4 2>&1
   check 4 0 ./t4 '3208236297 413'

   ${MAILX} ${ARGS} -S on-mailbox-open=fh-test -X 'define fh-test {
         echo fh-test to; set autosort=to showto
      }' -fH ./t.mbox > ./t5 2>&1
   check 5 0 ./t5 '901727002 411'

   check 6 - ./t.mbox '3540578520 839'

   t_epilog "${@}"
} # }}}

t_newmail_on_folder() { #{{{ (renamed to on-mailbox)
   t_prolog "${@}"

   t__gen_msg from 'ex1@am.ple' subject s1 > ./t1x.mbox
   t__gen_msg from 'ex2@am.ple' subject s2 > ./t1y.mbox
   t__gen_msg from 'ex3@am.ple' subject s3 > ./t1z.mbox

   xfolder=`${pwd}`

   #{{{
   ${MAILX} ${ARGS} -Y '#
      set noautosort noshowto \
         on-mailbox-open=ofo on-mailbox-newmail=ofn \
         on-mailbox-open-+t1x.mbox=ofo-z on-mailbox-newmail-+t1x.mbox=ofn-z \
         on-mailbox-open-+t1y.mbox=ofo-z on-mailbox-newmail-+t1y.mbox=ofn-z
      define ofo {
         echo "ofo as<$autosort> showto<$showto> kuh<$kuh>"
         set autosort=to showto kuh=muh
      }
      define ofn {
         echo "ofn as<$autosort> showto<$showto> kuh<$kuh>"
         set autosort=from kuh=wuff
         search:u
      }
      define ofo-z {
         echo ofo-z; \xcall ofo
      }
      define ofn-z {
         echo ofn-z; \xcall ofn
      }
      echo "1 as<$autosort> showto<$showto> kuh<$kuh>"
      File ./t1x.mbox
      echo "2 as<$autosort> showto<$showto> kuh<$kuh>"
      newmail
      echo "3 as<$autosort> showto<$showto> kuh<$kuh>"
      !'"${cat}"' ./t1y.mbox >> ./t1x.mbox
      echo "4 as<$autosort> showto<$showto> kuh<$kuh>"
      newmail
      echo "5 as<$autosort> showto<$showto> kuh<$kuh>"
      headers
      #
      set folder='"${xfolder}"'
      echo "11 as<$autosort> showto<$showto> kuh<$kuh>"
      File +t1y.mbox
      echo "12 as<$autosort> showto<$showto> kuh<$kuh>"
      newmail
      echo "13 as<$autosort> showto<$showto> kuh<$kuh>"
      !'"${cat}"' ./t1z.mbox >> ./t1y.mbox
      echo "14 as<$autosort> showto<$showto> kuh<$kuh>"
      newmail
      echo "15 as<$autosort> showto<$showto> kuh<$kuh>"
      headers
      ' \
      -R > ./t1 2>&1
   #}}}
   check 1 0 ./t1 '3709091196 1000'

   t_epilog "${@}"
} # }}}

t_q_t_etc_opts() { # {{{
   # Simple, if we need more here, place in a later vim fold!
   t_prolog "${@}"

   # Three tests for MIME encoding and (a bit) content classification.
   # At the same time testing -q FILE, < FILE and -t FILE
   t__put_body > ./t.in

   < ./t.in ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -a ./t.in -s "`t__put_subject`" ./t1
   check 1 0 ./t1 '589355579 6642'

   < /dev/null ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -a ./t.in -s "`t__put_subject`" -q ./t.in ./t2
   check 2 0 ./t2 '589355579 6642'

   (  echo "To: ./t3" && echo "Subject: `t__put_subject`" && echo &&
      ${cat} ./t.in
   ) | ${MAILX} ${ARGS} ${ADDARG_UNI} -Snodot -a ./t.in -t
   check 3 0 ./t3 '589355579 6642'

   # Check comments in the header
   ${cat} <<-'_EOT' | ${MAILX} ${ARGS} -Snodot -t ./t4
		# Ein Kommentar
		From: du@da
		# Noch ein Kommentar
		Subject    :       hey you
		# Nachgestelltes Kommentar
		
		BOOOM
		_EOT
   check 4 0 ./t4 '1256637859 138'

   # ?MODifier suffix
   printf '' > ./t10
   (  echo 'To?single    : ./t.to1 t.to1-2  ' &&
      echo 'CC: ./t.cc1 ./t.cc2' &&
      echo 'BcC?sin  : ./t.bcc1 .t.bcc1-2 ' &&
      echo 'To?    : ./t.to2 .t.to2-2 ' &&
      echo &&
      echo body
   ) | ${MAILX} ${ARGS} ${ADDARG_UNI} -Snodot -t -Smta=test://t10
   check 5 0 './t.to1 t.to1-2' '2948857341 94'
   check 6 - ./t.cc1 '2948857341 94'
   check 7 - ./t.cc2 '2948857341 94'
   check 8 - './t.bcc1 .t.bcc1-2' '2948857341 94'
   check 9 - './t.to2 .t.to2-2' '2948857341 94'
   check 10 - ./t10 '4294967295 0'

   t_epilog "${@}"
} # }}}

t_message_injections() { # {{{
   # Simple, if we need more here, place in a later vim fold!
   t_prolog "${@}"

   echo mysig > ./t.mysig

   echo some-body | ${MAILX} ${ARGS} -Smta=test://t1 \
      -Smessage-inject-head=head-inject \
      -Smessage-inject-tail="`${cat} ./t.mysig`"'\ntail-inject' \
      ex@am.ple > ./t2 2>&1
   check 1 0 ./t1 '701778583 143'
   check 2 - ./t2 '4294967295 0'

   ${cat} <<-'_EOT' > ./t2.in
	From: me
	To: ex1@am.ple
	Cc: ex2@am.ple
	Subject: This subject is

   Body, body, body me.
	_EOT
   < ./t2.in ${MAILX} ${ARGS} -t -Smta=test://t3 \
      -Smessage-inject-head=head-inject \
      -Smessage-inject-tail="`${cat} ./t.mysig`\n"'tail-inject' \
      > ./t4 2>&1
   check 3 0 ./t3 '2646789247 218'
   check 4 - ./t4 '4294967295 0'

   t_epilog "${@}"
} # }}}

t_attachments() { # {{{
   # TODO More should be in compose mode stuff aka digmsg
   t_prolog "${@}"

   ${cat} <<-'_EOT' > ./tx.box
	From steffen Sun Feb 18 02:48:40 2018
	Date: Sun, 18 Feb 2018 02:48:40 +0100
	To:
	Subject: m1
	User-Agent: s-nail v14.9.7
	
	
	From steffen Sun Feb 18 02:48:42 2018
	Date: Sun, 18 Feb 2018 02:48:42 +0100
	To:
	Subject: m2
	User-Agent: s-nail v14.9.7
	
	
	_EOT
   echo att1 > ./tx.a1
   printf 'att2-1\natt2-2\natt2-4\n' > ./'tx a2'
   printf 'att3-1\natt3-2\natt3-4\n' > ./txa3
   printf 'att4-1\natt4-2\natt4-4\n' > './tx a4'

   printf \
'!@  ./txa3              "./tx a4"             ""
!p
!@
   ./txa3
 "./tx a2"

!p
!.' \
   | ${MAILX} ${ARGS} -Sescape=! -Smta=test://t1 \
      -a ./tx.a1 -a './tx a2' \
      -s attachment-test \
      ex@am.ple > ./t2 2>&1

   check 1 0 ./t1 '113047025 646'
   if have_feat uistrings; then
      check 2 - ./t2 '3897935448 734'
   else
      t_echoskip '2:[!UISTRINGS]'
   fi

   #{{{
   printf \
'mail ex@amp.ple
!s This the subject is
!@  ./txa3        "#2"      "./tx a4"          "#1"   ""
!p
!@
   "./tx a4"
 "#2"

!p
!.
      mail ex@amp.ple
!s Subject two
!@  ./txa3        "#2"      "./tx a4"          "#1"   ""
!p
!@

!p
!.
      mail ex@amp.ple
!s Subject three
!@  ./txa3     ""   "#2"    ""  "./tx a4"   ""       "#1"   ""
!p
!@
 ./txa3

!p
!.
      mail ex@amp.ple
!s Subject Four
!@  ./txa3     ""   "#2"    ""  "./tx a4"   ""       "#1"   ""
!p
!@
 "#1"

!p
!.
      mail ex@amp.ple
!s Subject Five
!@
 "#2"

!p
!.' \
   | ${MAILX} ${ARGS} -Sescape=! -Smta=test://t3 -Rf ./tx.box \
         > ./t4 2>&1
   #}}}
   check 3 0 ./t3 '542557236 2337'
   if have_feat uistrings; then
      check 4 - ./t4 '2071033724 1930'
   else
      t_echoskip '4:[!UISTRINGS]'
   fi

   #{{{
   printf \
'mail ex@amp.ple
!s Subject One
!@ "#."
Body one.
!p
!.
from 2
mail ex@amp.ple
!s Subject Two
!@
      "#."

Body two.
!p
!.
reply 1 2
!@ "#."
!p
!.
!@
"#."

!p
!.' \
   | ${MAILX} ${ARGS} -Sescape=! -Smta=test://t5 -Rf ./tx.box \
         > ./t6 2>&1
   #}}}
   check 5 0 ./t5 '1604688179 2316'
   if have_feat uistrings; then
      check 6 - ./t6 '1210753005 508'
   else
      t_echoskip '6:[!UISTRINGS]'
   fi

   ##

   # Content-ID:
   </dev/null ${MAILX} ${ARGS} -Smta=test \
      -Sstealthmua=noagent -Shostname \
      -a ./tx.a1 -a './tx a2' \
      -a ./txa3 -a './tx a4' \
      -s Y \
      ex@am.ple > ./t7 2>&1
   check 7 0 ./t7 '2357640864 1313'

   # input charset
   </dev/null ${MAILX} ${ARGS} -Smta=test -Sttycharset=utf8 \
      -a ./tx.a1=ascii -a './tx a2'=LATin1 \
      -a ./txa3=UTF-8 -a './tx a4'=- \
      -s Y \
      ex@am.ple > ./t8 2>&1
   check 8 0 ./t8 '2442101854 926'

   # input+output charset, no iconv
   </dev/null ${MAILX} ${ARGS} -Smta=test \
      -a ./tx.a1=ascii#- -a './tx a2'=LATin1#- \
      -a ./txa3=UTF-8#- -a './tx a4'=utf8#- \
      -s Y \
      ex@am.ple > ./t9 2>&1
   check 9 0 ./t9 '2360896571 938'

   if have_feat iconv; then
      printf 'ein \303\244ffchen und ein pferd\n' > ./txa10
      if (< ./tx.a1 iconv -f ascii -t utf8) >/dev/null 2>&1; then
         </dev/null ${MAILX} ${ARGS} --set mta=test \
            --set stealthmua=noagent --set hostname \
            --attach ./tx.a1=-#utf8 \
            --attach ./txa10=utf8#latin1 \
            --subject Y \
            ex@am.ple > ./t10 2>&1
         check 10 0 ./t10 '75186432 923'
      else
         t_echoskip '10:[ICONV/iconv(1):missing conversion(1)]'
      fi
   else
      t_echoskip '10:[!ICONV]'
   fi

   # `mimetype' "handler-only" type-marker not matched for types
   </dev/null ${MAILX} ${ARGS} -Smta=test \
      -s Y \
      -Y '~:unmimetype *' \
      -Y '~:mimetype ? application/yeye txa3' \
      -Y '~:mimetype ?* application/nono txa3' \
      -Y '~@ ./txa3' \
      ex@am.ple > ./t11 2>&1
   check 11 0 ./t11 '602355274 477'

   # default conv with ! base64 enforcement, too
   printf 'one line\012' > ./tcnv
   check cnv-1.0 - ./tcnv  '665489461 9'

   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv t@t > ./tcnv-1.1 2>&1
   check cnv-1.1 0 ./tcnv-1.1 '1383471891 569'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=# t@t > ./tcnv-1.2 2>&1
   check cnv-1.2 0 ./tcnv-1.2 '1383471891 569'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=-# t@t > ./tcnv-1.3 2>&1
   check cnv-1.3 0 ./tcnv-1.3 '1383471891 569'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=#- t@t > ./tcnv-1.4 2>&1
   check cnv-1.4 0 ./tcnv-1.3 '1383471891 569'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=-#- t@t > ./tcnv-1.5 2>&1
   check cnv-1.5 0 ./tcnv-1.5 '1383471891 569'
   </dev/null ${MAILX} ${ARGS} -Y 'write;xit' -Rf ./tcnv-1.5 \
      > ./tcnv-1.5.verify 2>&1
   check cnv-1.5.verify 0 ./tcnv-1.5.verify '3289363155 36'
   check cnv-1.5.write - ./tcnv#1.2 '665489461 9'

   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=!# t@t > ./tcnv-1.6 2>&1
   check cnv-1.6 0 ./tcnv-1.6 '1023221665 607'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=!-# t@t > ./tcnv-1.7 2>&1
   check cnv-1.7 0 ./tcnv-1.7 '1023221665 607'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=#!- t@t > ./tcnv-1.8 2>&1
   check cnv-1.8 0 ./tcnv-1.8 '1023221665 607'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=-#!- t@t > ./tcnv-1.9 2>&1
   check cnv-1.9 0 ./tcnv-1.9 '1023221665 607'
   </dev/null ${MAILX} ${ARGS} -Y 'write;xit' -Rf ./tcnv-1.9 \
      > ./tcnv-1.9.verify 2>&1
   check cnv-1.9.verify 0 ./tcnv-1.9.verify '3289363155 36'
   check cnv-1.9.write - ./tcnv#1.2#1.2 '665489461 9'

   printf 'one line\015\012' > ./tcnv
   check cnv-2.0 - ./tcnv '3312016702 10'

   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv t@t > ./tcnv-2.1 2>&1
   check cnv-2.1 0 ./tcnv-2.1 '286434252 671'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=# t@t > ./tcnv-2.2 2>&1
   check cnv-2.2 0 ./tcnv-2.2 '286434252 671'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=-# t@t > ./tcnv-2.3 2>&1
   check cnv-2.3 0 ./tcnv-2.3 '286434252 671'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=#- t@t > ./tcnv-2.4 2>&1
   check cnv-2.4 0 ./tcnv-2.3 '286434252 671'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=-#- t@t > ./tcnv-2.5 2>&1
   check cnv-2.5 0 ./tcnv-2.5 '286434252 671'
   </dev/null ${MAILX} ${ARGS} -Y 'write;xit' -Rf ./tcnv-2.5 \
      > ./tcnv-2.5.verify 2>&1
   check cnv-2.5.verify 0 ./tcnv-2.5.verify '3289363155 36'
   check cnv-2.5.write - ./tcnv#1.2#1.2#1.2 '3312016702 10'

   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=!# t@t > ./tcnv-2.6 2>&1
   check cnv-2.6 0 ./tcnv-2.6 '3314429964 662'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=!-# t@t > ./tcnv-2.7 2>&1
   check cnv-2.7 0 ./tcnv-2.7 '3314429964 662'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=#!- t@t > ./tcnv-2.8 2>&1
   check cnv-2.8 0 ./tcnv-2.8 '3314429964 662'
   < ./tcnv ${MAILX} ${ARGS} -Smta=test -a ./tcnv=-#!- t@t > ./tcnv-2.9 2>&1
   check cnv-2.9 0 ./tcnv-2.9 '3314429964 662'
   </dev/null ${MAILX} ${ARGS} -Y 'write;xit' -Rf ./tcnv-2.9 \
      > ./tcnv-2.9.verify 2>&1
   check cnv-2.9.verify 0 ./tcnv-2.9.verify '3289363155 36'
   check cnv-2.9.write - ./tcnv#1.2#1.2#1.2#1.2 '3312016702 10'

   t_epilog "${@}"
} # }}}

t_rfc2231() { # {{{
   # (after attachments) 
   t_prolog "${@}"

   (
      mkdir ./ttt || exit 1
      cd ./ttt || exit 2

      : > "ma'ger.txt"
      : > "mä'ger.txt"
      : > 'diet\ is \curd.txt'
      : > 'diet "is" curd.txt'
      : > höde-tröge.txt
      : > höde__tröge__müde__dätte__hätte__vülle__gülle__äse__äße__säuerliche__kräuter__österliche__grüße__mäh.txt
      : > höde__tröge__müde__dätte__hätte__vuelle__guelle__aese__aesse__sauerliche__kräuter__österliche__grüße__mäh.txt
      : > hööööööööööööööööö_nöööööööööööööööööööööö_düüüüüüüüüüüüüüüüüüü_bäääääääääääääääääääääääh.txt
      : > ✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆.txt
   )

   echo bla | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -a "./ttt/ma'ger.txt" -a "./ttt/mä'ger.txt" \
      -a './ttt/diet\ is \curd.txt' -a './ttt/diet "is" curd.txt' \
      -a ./ttt/höde-tröge.txt \
      -a ./ttt/höde__tröge__müde__dätte__hätte__vülle__gülle__äse__äße__säuerliche__kräuter__österliche__grüße__mäh.txt \
      -a ./ttt/höde__tröge__müde__dätte__hätte__vuelle__guelle__aese__aesse__sauerliche__kräuter__österliche__grüße__mäh.txt \
      -a ./ttt/hööööööööööööööööö_nöööööööööööööööööööööö_düüüüüüüüüüüüüüüüüüü_bäääääääääääääääääääääääh.txt \
      -a ./ttt/✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆.txt \
      ./t1
   check 1 0 ./t1 '3720896054 3088'

   # `resend' test, reusing $MBOX
   printf "Resend ./t2\nx\n" | ${MAILX} ${ARGS} -Rf ./t1
   check 2 0 ./t2 '3720896054 3088'

   printf "resend ./t3\nx\n" | ${MAILX} ${ARGS} -Rf ./t1
   check 3 0 ./t3 '3979736592 3133'

   #{{{ And a primitive test for reading messages with invalid parameters
   ${cat} <<-'_EOT' > ./t.inv
	From a@b.invalid Wed May 15 12:43:00 2018
	MIME-Version: 1.0
	Content-Type: multipart/mixed; boundary="1"
	
	This is a multi-part message in MIME format.
	--1
	Content-Type: text/plain; charset=UTF-8
	Content-Transfer-Encoding: quoted-printable
	
	foo
	--1
	Content-Type: text/plain; name*17="na"; name*18="me-c-t"
	Content-Transfer-Encoding: 7bit
	Content-Disposition: inline
	
	bar
	--1--
	
	From a@b.invalid Wed May 15 12:43:00 2018
	MIME-Version: 1.0
	Content-Type: multipart/mixed; boundary="2"
	
	This is a multi-part message in MIME format.
	--2
	Content-Type: text/plain; charset=UTF-8
	Content-Transfer-Encoding: quoted-printable
	
	foo
	--2
	Content-Type: text/plain; name*17="na"; name*18="me-c-t"
	Content-Transfer-Encoding: 7bit
	Content-Disposition: inline;
	        filename*0="na";
	        filename*998999999999999999999999999999="me-c-d"
	
	bar
	--2--
	
	From a@b.invalid Wed May 15 12:43:00 2018
	MIME-Version: 1.0
	Content-Type: multipart/mixed; boundary="3"
	
	This is a multi-part message in MIME format.
	--3
	Content-Type: text/plain; charset=UTF-8
	Content-Transfer-Encoding: quoted-printable
	
	foo
	--3
	Content-Type: text/plain; name*17="na"; name*18="me-c-t"
	Content-Transfer-Encoding: 7bit
	Content-Disposition: inline;
	        filename*0="na"; filename*998="me-c-d"
	
	bar
	--3--
	_EOT
   #}}}
   printf '\\#
      \\headerpick type ignore Content-Type Content-Disposition
      \\type 1 2 3
      \\xit
      ' | ${MAILX} ${ARGS} -Rf ./t.inv > ./t4 2> ./t5
   check 4 0 ./t4 '4094731083 905'
   if have_feat uistrings && have_feat iconv; then
      check 5 - ./t5 '3713266499 473'
   else
      t_echoskip '5:[!UISTRINGS or !ICONV]'
   fi

   t_epilog "${@}"
} # }}}

t_mimetype() { # {{{
   t_prolog "${@}"

   tmt='#
mimetype ? text/x-unix-readme README INSTALL TODO COPYING NEWS
mimetype application/x-tar-gz tgz tar.gz
mimetype application/x-ma-tar-gz ma.tar.gz
mimetype application/x-x-ma-tar-gz x.ma.tar.gz
mimetype application/x-fun x.tar
mimetype ?h application/x-xfun x
mimetype application/x-tar  tar
mimetype application/gzip  tgz gz emz
'

   # It prepends
   { printf 'mimetype\nxit\n' | ${MAILX} -Y "${tmt}" ${ARGS} |
      ${sed} '9,$d' > ./t1; } 2> ./t2
   check 1 0 ./t1 '668594290 337'
   check 2 - ./t2 '4294967295 0'

   # It classifies
   tfs='README x.gz x.ma.tar.gz x.tar x.tar.gz '\
'y.x.ma.tar.gz .x .x.ma.tar.gz .x.tar y.x.tar x.x NEWS'

   for f in ${tfs}; do printf '' > ./${f}; done

   printf 'm ./t3\nLine1\n~@ %s\n~.\nxit' "${tfs}" |
      ${MAILX} -Y "${tmt}" ${ARGS} > ./t4 2>&1
   check 3 0 ./t3 '2606517416 2221'
   check 4 - ./t4 '4294967295 0'

   # Cache management
   printf '#!%s\n%s | %s '"'2,"'$'"d'"' > ./t6 2>&1\n' \
      "${SHELL}" "${cat}" "${sed}" > ./t5p.sh
   chmod 0755 ./t5p.sh
   printf 'application/boom peng' > ./t5mt

   ${MAILX} ${ARGS} -S PAGER=./t5p.sh -Y "${tmt}" -Y '#
echo 1
unmimetype application/gzip text/x-unix-readme application/x-fun *
echo 2
;mimetype;echo 3;mimetype text/au AU;echo 4;mimetype text/au AU2;
echo 5;mimetype;echo 6;unmimetype text/au;echo 7;mimetype
echo 8;mimetype text/au AU;echo 9;mimetype text/au AU2;echo 10;mimetype
echo 11;unmimetype *;echo 12;mimetype;
echo 13;unmimetype reset # (re-) allow cache init
echo 14;set mimetypes-load-control=f=./t5mt;set crt=0;mimetype;unset crt
echo 15;unmimetype application/booms;echo 16;unmimetype application/boom;echo x
' \
         > ./t5 2>&1
   check_ex0 5-estat
   if have_feat uistrings; then
      check 5 - ./t5 '1352221656 322'
   else
      t_echoskip '5:[!UISTRINGS]'
   fi
   check 6 - ./t6 '3643354701 32'

   # Note: further type-marker stuff is done in t_pipe_handlers()

   t_epilog "${@}"
} # }}}

t_mime_types_load_control() { # {{{
   t_prolog "${@}"

   if have_feat uistrings; then :; else
      t_echoskip '[!UISTRINGS]'
      t_epilog "${@}"
      return
   fi

   ${cat} <<-'_EOT' > ./t.mts1
   ? application/mathml+xml mathml
	_EOT

   ${cat} <<-'_EOT' > ./t.mts2
   ? x-conference/x-cooltalk ice
   ?t aga-aga aga
   ? application/aga-aga aga
	_EOT

   ${cat} <<-'_EOT' > ./t.mts1.mathml
   <head>nonsense ML</head>
	_EOT

   ${cat} <<-'_EOT' > ./t.mts2.ice
   Icy, icy road.
	_EOT

   printf 'of which the crack is coming soon' > ./t.mtsx.doom
   printf 'of which the crack is coming soon' > ./t.mtsx.aga

   printf '
         m ./t1_2-x
         Schub-di-du
~@ ./t.mts1.mathml
~@ ./t.mts2.ice
~@ ./t.mtsx.doom
~@ ./t.mtsx.aga
~.
         File ./t1_2-x
         from*
         type
         xit
      ' | ${MAILX} ${ARGS} \
         -Smimetypes-load-control=f=./t.mts1,f=./t.mts2 \
         > ./t1 2>&1
   check_ex0 1-estat
   ${cat} ./t1_2-x >> ./t1
   check 1 - ./t1 '2924385241 2476'

   echo type | ${MAILX} ${ARGS} -R \
      -Smimetypes-load-control=f=./t.mts1,f=./t.mts3 \
      -f ./t1_2-x > ./t2 2>&1
   check 2 0 ./t2 '2867276834 1213'

   t_epilog "${@}"
} # }}}
# }}}

# Around state machine, after basics {{{
t_alternates() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -Smta=test://t1 > ./t2 2>&1
   commandalias x echo '$?/$^ERRNAME'
   commandalias y echo '$?/$^ERRNAME <$rv>'
   echo --0
   alternates;x
   alternates a1@b1 a2@b2 a3@b3;x
   alternates;x
   vput alternates rv;y

   echo --1
   unalternates a2@b2
   vput alternates rv;y
   unalternates a3@b3
   vput alternates rv;y
   unalternates a1@b1
   vput alternates rv;y

   echo --2
   unalternates *
   alternates a1@b1 a2@b2 a3@b3
   unalternates a3@b3
   vput alternates rv;y
   unalternates a2@b2
   vput alternates rv;y
   unalternates a1@b1
   vput alternates rv;y

   echo --3
   alternates a1@b1 a2@b2 a3@b3
   unalternates a1@b1
   vput alternates rv;y
   unalternates a2@b2
   vput alternates rv;y
   unalternates a3@b3
   vput alternates rv;y

   echo --4
   unalternates *
   alternates a1@b1 a2@b2 a3@b3
   unalternates *
   vput alternates rv;y

   echo --5
   unalternates *
   alternates a1@b1 a1@c1 a1@d1 a2@b2 a3@b3 a3@c3 a3@d3
   m a1@b1 a1@c1 a1@d1
	~s all alternates, only a1@b1 remains
	~c a2@b2
	~b a3@b3 a3@c3 a3@d3
	~r - '_EOT'
   This body is!
   This also body is!!
_EOT
	~.

   echo --6
   unalternates *
   alternates a1@b1 a1@c1 a2@b2 a3@b3
   m a1@b1 a1@c1 a1@d1
	~s a1@b1 a1@d1, and a3@c3 a3@d3 remain
	~c a2@b2
	~b a3@b3 a3@c3 a3@d3
	~r - '_EOT'
   This body2 is!
_EOT
	~.

   echo --7
   alternates a1@b1 a2@b2 a3; set allnet
   m a1@b1 a1@c1 a1@d1
	~s all alternates via allnet, only a1@b1 remains
	~c a2@b2
	~b a3@b3 a3@c3 a3@d3
	~r - '_EOT'
   This body3 is!
_EOT
	~.

   echo --10
   unalternates *
   alternates a1@b1;x
   vput alternates rv;y
   alternates a2@b2;x
   vput alternates rv;y
   alternates a3@b3;x
   vput alternates rv;y
   alternates a4@b4;x
   vput alternates rv;y

   unalternates *
   vput alternates rv;y

   echo --11
   set posix
   alternates a1@b1 a2@b2;x
   vput alternates rv;y
   alternates a3@b3 a4@b4;x
   vput alternates rv;y
	__EOT
   #}}}
   check 1 0 ./t1 '3901995195 542'
   if have_feat uistrings; then
      check 2 - ./t2 '1878598364 505'
   else
      t_echoskip '2:[!UISTRINGS]'
   fi

   #{{{ Automatic alternates, also from command line (freezing etc.)
   ${cat} <<- '__EOT' > ./t3_5-in
	From trouble-report@desy  Wed Jun  6 20:19:28 2018
	Date: Wed, 06 Jun 2018 19:58:02 +0200
	From: a@b.org, b@b.org, c@c.org
	Sender: a@b.org
	To: b@b.org
	Cc: a@b.org, c@c.org
	Subject: test
	Message-ID: <20180606175802.dw-cn%a@b.org>
	
	sultry
	
	__EOT

   printf '#
   reply
!^header remove to
!^header remove cc
!^header remove subject
!^header insert to b@b.org
!^header insert cc "a@b.org  b@b.org c@c.org"
my body
!.
   ' | ${MAILX} ${ARGS} -Smta=test://t3 -Sescape=! \
         -S from=a@b.org,b@b.org,c@c.org -S sender=a@b.org \
         -Rf ./t3_5-in > ./t4 2>&1
   #}}}
   check 3 0 ./t3 '917782413 299'
   check 4 - ./t4 '3604001424 44'

   # same, per command
   printf '#
   set from=a@b.org,b@b.org,c@c.org sender=a@b.org
   reply
!^header remove to
!^header remove cc
!^header remove subject
!^header insert to b@b.org
!^header insert cc "a@b.org  b@b.org c@c.org"
my body
!.
   ' | ${MAILX} ${ARGS} -Smta=test://t5 -Sescape=! \
         -Rf ./t3_5-in > ./t6 2>&1
   check 5 0 ./t5 '917782413 299'
   check 6 - ./t6 '3604001424 44'

   # And more, with/out -r (and that Sender: vanishs as necessary)
   # TODO -r should be the Sender:, which should automatically propagate to
   # TODO From: if possible and/or necessary.  It should be possible to
   # TODO suppres -r stuff from From: and Sender:, but fallback to special -r
   # TODO arg as appropriate.
   # TODO For now we are a bit messy

   </dev/null ${MAILX} ${ARGS} -Smta=test://t7 -s '-Sfrom + -r ++ test' \
      -c a@b.example -c b@b.example -c c@c.example \
      -S from=a@b.example,b@b.example,c@c.example \
      -S sender=a@b.example \
      -r a@b.example b@b.example ./t8 >./t9 2>&1
   check 7 0 ./t7 '683070437 201'
   check 8 - ./t8 '683070437 201'
   check 9 - ./t9 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=test://t10 -s '-Sfrom + -r ++ test' \
      -c a@b.example -c b@b.example -c c@c.example \
      -S from=a@b.example,b@b.example,c@c.example \
      -S sender=a2@b.example \
      -r a@b.example b@b.example ./t11 >./t12 2>&1
   check 10 0 ./t10 '1590152680 222'
   check 11 - ./t11 '1590152680 222'
   check 12 - ./t12 '4294967295 0'

   </dev/null ${MAILX} ${ARGS} -Smta=test://t13 -s '-Sfrom + -r ++ test' \
      -c a@b.example -c b@b.example -c c@c.example \
      -S from=a@b.example,b@b.example,c@c.example \
      -S sender=a@b.example \
      b@b.example >./t14 2>&1
   check 13 0 ./t13 '2530102496 273'
   check 14 - ./t14 '4294967295 0'

   t_epilog "${@}"
} # }}}

t_cmd_escapes() { # {{{
   # quote and cmd escapes because this (since Mail times) is worked in the
   # big collect() monster of functions
   t_prolog "${@}"

   echo 'included file' > ./t.txt
   { t__x1_msg && t__x2_msg && t__x3_msg &&
      t__gen_msg from 'ex4@am.ple' subject sub4 &&
      t__gen_msg from 'eximan <ex5@am.ple>' subject sub5 &&
      t__gen_mimemsg from 'ex6@am.ple' subject sub6; } > ./t.mbox
   check 1 - ./t.mbox '517368276 2182'

   # ~@ is tested with other attachment stuff, ~^ is in compose_edits + digmsg
   #{{{
   printf '#
      set Sign=SignVar sign=signvar DEAD=./t.txt
      set forward-inject-head quote-inject-head
      headerpick type retain Subject
      headerpick forward retain Subject To
      reply 2
!!1 Not escaped.  And shell test last, right before !..
!:echo 1
!:   echo 2 only echoed via colon
!:echo 2:$?/$^ERRNAME
!_  echo 3 only echoed via underscore
!:echo 3:$?/$^ERRNAME
!< ./t.txt
!:echo 4:$?/$^ERRNAME
!<! echo 5 shell echo included
!:echo 5:$?/$^ERRNAME
!| echo 6 pipecmd-pre; cat; echo 6 pipecmd-post
!:echo 6:$?/$^ERRNAME
7 and 8 are ~A and ~a:
!A
!:echo 7:$?/$^ERRNAME
!a
!:echo 8:$?/$^ERRNAME
!b 9 added ~b cc <ex1@am.ple>
!:echo 9:$?/$^ERRNAME
!c 10 added ~c c <ex2@am.ple>
!:echo 10:$?/$^ERRNAME
11 next ~d / $DEAD
!d
!:echo 11:$?/$^ERRNAME
12: ~F
!F
!:echo 12:$?/$^ERRNAME
13: ~F 1 3
!F 1 3
!:echo 13:$?/$^ERRNAME
!F 1000
!:echo 13-1:$?/$^ERRNAME; set posix
14: ~f (headerpick: subject)
!f
!:echo 14:$?/$^ERRNAME; unset posix forward-inject-head quote-inject-head
14.1: ~f (!posix: injections; headerpick: subject to)
!f
!:echo 14.1:$?/$^ERRNAME; set forward-add-cc
14.2: ~f (!posix: headerpick: subject to; forward-add-cc adds mr3)
!f 3
!:echo 14.2:$?/$^ERRNAME; set fullnames
14.3: ~f (!posix: headerpick: subject to; forward-add-cc adds mr1 fullname)
!f 1
!:echo 14.3:$?/$^ERRNAME; set nofullnames noforward-add-cc posix
15: ~f 1
!f 1
!:echo 15:$?/$^ERRNAME
15.5: nono: ~H, ~h
!H
!:echo 15.5-1:$?/$^ERRNAME
!h
!:echo 15.5-2:$?/$^ERRNAME
16, 17: ~I Sign, ~i Sign
!I Sign
!:echo 16:$?/$^ERRNAME
!i Sign
!:echo 17:$?/$^ERRNAME
18: ~M
!M
!:echo 18:$?/$^ERRNAME # XXX forward-add-cc: not expl. tested
19: ~M 1
!M 1
!:echo 19:$?/$^ERRNAME
20: ~m
!m
!:echo 20:$?/$^ERRNAME # XXX forward-add-cc: not expl. tested
21: ~m 3
!m 3
!:echo 21:$?/$^ERRNAME
!: # Initially ~Q was _exactly_ like
28,29 nothing, 30-34: ~Q
!:echo quote=<$quote>
30: ~Q
!Q
!:echo 30:$?/$^ERRNAME
31: ~Q 1 3
!Q 1 3
!:echo 31:$?/$^ERRNAME
set quote-inject-head quote-inject-tail indentprefix
!:set quote-inject-head=%%a quote-inject-tail=--%%r
32: ~Q
!Q
!:echo 32:$?/$^ERRNAME
set noquote-inject-head noquote-inject-tail quote-add-cc
!:set noquote-inject-head noquote-inject-tail quote-add-cc
33: ~Q 4
!Q 4
!:echo 33:$?/$^ERRNAME
set fullnames
!:set fullnames
34: ~Q 5
!Q 5
!:echo 34:$?/$^ERRNAME
unset fullnames, quote stuff
!:unset quote quote-add-cc fullnames
22: ~R ./t.txt
!R ./t.txt
!:echo 22:$?/$^ERRNAME
23: ~r ./t.txt
!r ./t.txt
!:echo 23:$?/$^ERRNAME
24: ~s this new subject
!s 24 did new ~s ubject
!:echo 24:$?/$^ERRNAME
!t 25 added ~t o <ex3@am.ple>
!:echo 25:$?/$^ERRNAME
26.1: ~U
!U
!:echo 26.1:$?/$^ERRNAME
26.2: ~U 1
!U 1
!:echo 26.2:$?/$^ERRNAME # XXX forward-add-cc: not expl. tested
27.1: ~u
!u
!:echo 27.1:$?/$^ERRNAME
27.2: ~u 1
!u 1
!:echo 27.2:$?/$^ERRNAME # XXX forward-add-cc: not expl. tested
and i ~w rite this out to ./t3
!w ./t3
!:echo i ~w:$?/$^ERRNAME
!:set x=$escape;set escape=~
~!echo shell command output
~:echo shell:$?/$^ERRNAME
~:set escape=$x
50:F
!F 6
!:echo 50 was F:$?/$^ERRNAME
51:f
!f 6
!:echo 51 was f:$?/$^ERRNAME
52:M
!M 6
!:echo 52 was M:$?/$^ERRNAME
53:m
!m 6
!:echo 53 was m:$?/$^ERRNAME; set quote
54:Q
!Q 6
!:echo 54 was Q:$?/$^ERRNAME
55:U
!U 6
!:echo 55 was U:$?/$^ERRNAME
56:u
!u 6
!:echo 56 was u:$?/$^ERRNAME
!.
   ' | ${MAILX} ${ARGS} -Smta=test://t2 -Rf \
         -Sescape=! -Sindentprefix=' |' \
         ./t.mbox >./t2-x 2>./t2-err
   #}}}
   check_ex0 2-estat
   ${cat} ./t2-x >> t2
   have_feat filter-html-tagsoup && ck='4240573679 7693' ||
      ck='72945 7753'
   check 2 - ./t2 "${ck}"
   if have_feat uistrings && have_feat iconv; then
      check 2-err - ./t2-err '3575876476 49'
   else
      t_echoskip '2-err:[!UISTRINGS or !ICONV]'
   fi
   check 3 - ./t3 '1594635428 4442'

   #{{{ Simple return/error value after *expandaddr* failure test
   printf 'body
!:echo --one
!s This a new subject is
!:set expandaddr=-name
!t two@to.invalid
!:echo $?/$^ERRNAME
!:echo --two
!c no-name-allowed
!:echo $?/$^ERRNAME
!c one@cc.invalid
!:echo $?/$^ERRNAME
!:echo --three
!:alias abcc one@bcc.invalid
!b abcc
!:echo $?/$^ERRNAME
!:set expandaddr=+addr
!b abcc
!:echo $!/$?/$^ERRNAME
!.
   ' | ${MAILX} ${ARGS} -Smta=test://t4 \
         -Sescape=! \
         -s testsub one@to.invalid >./t5 2>&1
   #}}}
   check_ex0 4-estat
   check 4 0 ./t4 "3422189437 200"
   have_feat uistrings && ck='2336041127 212' || ck='1818580177 59'
   check 5 - ./t5 "${ck}"

   # Modifiers and whitespace indulgence; first matches t_eval():1
   #{{{
   ${cat} <<- '__EOT' | \
      ${MAILX} ${ARGS} -Smta=test://t7 -Sescape=! -Spwd="`${pwd}`" \
         -s testsub one@to.invalid >./t6 2>./t8
		body
		!:set i=du
		!:echo 1:
		! : echo $i
		!  :  echo '$i'
		!$:echo '$i'
		!:echo 2:
		!:echo "\"'$i'\""
		!$:echo "\"'$i'\""
		!$$:echo "\"'$i'\""
		! 	  $   $  $  $ : echo "\"'$i'\""
		! :echo one
		! 		  <./t.nosuch
		!               :echo two
		!   :     set i=./t.nosuch
		! 	  -  	  $ 	 <  	   $i
		!:echo three
		!   :     set \
		              	          errexit
		! 	  -  	$  <   $i
		!-$: echo four
		!$<  	   ./t.nosuch
		! 	 :   	 echo five
		__EOT
   #}}}
   check 6 4 ./t6 '892731775 136'
   [ -f ./t7 ]; check_exn0 7
   if have_feat uistrings; then
      check 8 - ./t8 '472073999 207'
   else
      t_echoskip '8:[!UISTRINGS]'
   fi

   t_epilog "${@}"
} # }}}

t_compose_edits() { # {{{ XXX very rudimentary
   # after: t_cmd_escapes
   t_prolog "${@}"

   #{{{ Something to use as "editor"
   ${cat} <<-_EOT > ./ted.sh
	#!${SHELL}
	${cat} <<-__EOT > \${1}
	Fcc: t.out1
	To:
	Fcc: t.out2
	Subject: Fcc test 1
	Fcc: t.out3

	A body
	__EOT
	exit 0
	_EOT
   ${chmod} 0755 ted.sh
   #}}}

   printf 'mail ./t1\n~s This subject is\nThis body is\n~.' |
      ${MAILX} ${ARGS} -Seditheaders >./t2 2>&1
   check 1 0 ./t1 '3993703854 127'
   check 2 - ./t2 '4294967295 0'

   : > ./t6
   printf 'mail ./t6-x\n~s This subject is\nThis body is\n~e\n~.' |
      ${MAILX} ${ARGS} -Seditheaders -SEDITOR=./ted.sh >./t7 2>&1
   check 3 0 ./t.out1 '285981670 116'
   check 4 - ./t.out2 '285981670 116'
   check 5 - ./t.out3 '285981670 116'
   [ -f ./t6-x ]; check_exn0 6
   check 7 - ./t7 '4294967295 0'
   ${rm} ./t.out1 ./t.out2 ./t.out3

   #{{{ Note t_compose_hooks adds ~^ stress tests
   printf '#
   mail ./t8\n!s This subject is\nThis body is
!^header
!^header list
!^header list fcc
!^header show fcc
!^header remove to
!^header insert fcc            ./t8
!^header insert fcc      t9-x
!^header insert fcc   ./t10-x
!^header list
!^header show fcc
!^header remove-at fcc 2
!^header remove-at fcc 2
!^header show fcc
!^head remove fcc
!^header show fcc
!^header insert fcc ./t8
!^header show fcc
!^header list
!.
      ' | ${MAILX} ${ARGS} -Sescape=! >./t11 2>&1
   #}}}
   check 8 0 ./t8 '3993703854 127'
   [ -f ./t9-x ]; check_exn0 9
   [ -f ./t10-x ]; check_exn0 10
   check 11 - ./t11 '2907383252 330'

   ${cat} <<-_EOT | ${MAILX} ${ARGS} -t >./t13 2>&1
	Fcc: t12
	Subject: Fcc via -t test

	My body
	_EOT
   check 12 0 ./t12 '1289478830 122'
   check 13 - ./t13 '4294967295 0'

   #{{{ This test assumes code of `^' and `digmsg' is shared: see t_digmsg()
   echo 'b 1' > ./t14' x 1'
   echo 'b 2' > ./t14' x 2'
   printf '#
mail ./t15
!^header insert     subject      subject       
!:set i="./t14 x 1"
!^header list
!:echo =0
!^attachment
!:echo =1
!^attachment insert "$i"
!:echo =2
!^attachment
!:echo =3
!^attachment list
!:echo =4
!^attachment insert '"'"'./t14 x 2'"'"'
!:echo =5
!^attachment list
!:echo =6
!^attachment remove "$i"
!:echo =7
!^attachment list
!:echo =8
!^attachment insert $'"'"'\\$i'"'"'
!:echo =10
!^attachment list
!:echo =11
!^header list
!:echo =12
!^a a  $i
!:echo =13
!^a attribute-set  "$i"     filenames "  cannot wait  for you "
!:echo =14
!^a a  $i
!:echo =15
!^a attribute-set  "$i"     filename "  cannot wait  for you "
!:echo =16
!^a a  $i
!:echo =17
!^a attribute-at 2
!:echo =18
!^a attribute-set-at 2  "filename"   "private  eyes"
!:echo =19
!^a attribute-at 2
!:echo =20
!^a attribute-set-at 2 content-description "private c-desc"
!:echo =21
!^a attribute-at 2
!:echo =22
!^a attribute-set-at 2 content-ID "priv invd c-id"
!:echo =23
!^a attribute-at 2
!:echo =24
!^a attribute-set-at 2 content-TyPE tExT/mARkLO
!:echo =25
!^a attribute-at 2
!:echo =26
!^a attribute-set-at 2 content-TyPE ""
!:echo =27
!^a attribute-at 2
!:echo =28
!.
   ' | ${MAILX} ${ARGS} -Sescape=! >./t14 2>&1
   #}}}
   check 14 0 ./t14 '2420103204 1596'
   check 15 - ./t15 '3755507589 637'

   t_epilog "${@}"
} # }}}

t_digmsg() { # {{{ XXX rudimentary; <> compose_edits()?
   t_prolog "${@}"

   #{{{
   printf '#
   mail ./t1\n!s This subject is\nThis body is
!:echo --one
!:digmsg create - -
!:digmsg - header list
!:digmsg - header show subject
!:digmsg - header show to
!:digmsg - header remove to
!:digmsg - header list
!:digmsg - header show to
!:digmsg remove -
!:echo --two
!:digmsg create -
!:digmsg - header list;   readall x;   echon "<$x>";
!:digmsg - header show subject;readall x;echon "<$x>";;
!:digmsg remove -
!:echo --three
!:    # nothing here as is comment
!^header insert fcc   ./t3
!:echo --four
!:digmsg create - -
!:digmsg - header list
!:digmsg - header show fcc
!:echo --five
!^head remove fcc
!:echo --six
!:digmsg - header list
!:digmsg - header show fcc
!:digmsg - header insert fcc ./t3
!:echo --seven
!:digmsg remove -
!:echo bye
!.
   echo --hello again
   File ./t3
   echo --one
   digmsg create 1 -
   digmsg 1 header list
   digmsg 1 header show subject
   echo --two
   ! : > ./t4
   File ./t4
   echo --three
   digmsg 1 header list; echo $?/$^ERRNAME
   digmsg create -; echo $?/$^ERRNAME
   echo ==========
   ! %s ./t3 > ./t5
   ! %s "s/This subject is/There subject was/" < ./t3 >> ./t5
   File ./t5
   mail nowhere@exam.ple
!:echo ===1
!:digmsg create -; echo $?/$^ERRNAME;\\
   digmsg create 1; echo $?/$^ERRNAME;\\
   digmsg create 2; echo $?/$^ERRNAME
!:echo ===2.1
!:digmsg - h l;echo $?/$^ERRNAME;readall d;echo "$?/$^ERRNAME <$d>"
!:echo =2.2
!:digmsg 1 h l;echo $?/$^ERRNAME;readall d;echo "$?/$^ERRNAME <$d>"
!:echo =2.3
!^ h l
!:echo =2.4
!:digmsg 2 h l;echo $?/$^ERRNAME;readall d;echo "$?/$^ERRNAME <$d>"
!:echo ===3.1
!:digmsg - h s to;echo $?/$^ERRNAME;readall d;echo "$?/$^ERRNAME <$d>"
!:echo =3.2
!:digmsg 1 h s subject;echo $?/$^ERRNAME;readall d;echo "$?/$^ERRNAME <$d>"
!:echo =3.3
!^ h s to
!:echo =3.4
!:digmsg 2 h s subject;echo $?/$^ERRNAME;readall d;echo "$?/$^ERRNAME <$d>"
!:echo ==4.1
!:digmsg remove -; echo $?/$^ERRNAME;\\
   digmsg remove 1; echo $?/$^ERRNAME;\\
   digmsg remove 2; echo $?/$^ERRNAME;
!x
   echo ======= new game new fun!
   mail one@to.invalid
!s hossa
!:set expandaddr=-name
!:echo -oneo
!^ header insert to two@to.invalid
!:echo $?/$^ERRNAME
!:echo --two
!^ header insert cc no-name-allowed
!:echo $?/$^ERRNAME
!^ header insert cc one@cc.invalid
!:echo $?/$^ERRNAME
!:echo --three
!:alias abcc one@bcc.invalid
!^ header insert bcc abcc
!:echo $?/$^ERRNAME
!:set expandaddr=+addr
!^ header insert bcc abcc
!:echo $!/$?/$^ERRNAME
!.
   echo --bye
      ' "${cat}" "${sed}" |
      ${MAILX} ${ARGS} -Smta=test://t1 -Sescape=! >./t2 2>&1
   #}}}
   check 1 0 ./t1 '665881681 179'
   have_feat uistrings && ck='1692807677 1366' || ck='218732148 1097'
   check 2 - ./t2 "${ck}"
   check 3 - ./t3 '3993703854 127'
   check 4 - ./t4 '4294967295 0'
   check 5 - ./t5 '2157992522 256'

   # [1091b026c9c8bcd26ce95aa90e7327757f9c0f32] check
   # While here ensure IDNA decoding does not happen
   # t6-7 used for t9, too
   #{{{
   ${cat} >> ./t6-7.eml <<'_EOT'
Date: Tue, 20 Apr 2021 00:23:10 +0200
To: Hey <bose@xn--kndelbrste-fcb0f>
Subject: =?utf-8?Q?=C3=BCbject?=
MIME-Version: 1.0
Content-Type: text/plain; charset=us-ascii
_EOT

   printf '#
      define a_read_header_mline_res {
         local set name fullname
         read name fullname
         local set read=$? es=$! en=$^ERRNAME
         echo "read<$read> es=<$es> en<$en> name<$name> fullname<$fullname>"
         if $read -gt 0
            xcall a_read_header_mline_res
         elif $read -eq 0
            # That not! read name
         else
            echoerr err
            xit
         end
      }
      digmsg create 1
      digmsg 1 header show to
      read es
      echo "!=$! ?=$? es<$es>"
      call a_read_header_mline_res
      digmsg remove 1
      commandalias XY echo hui
      read xy
XY
      echo "back !<$!> ?<$?> xy<$xy>"
      ' | ${MAILX} ${ARGS} -Rf eml://./t6-7.eml >./t6 2>&1
   #}}}
   check 6 0 ./t6 '1687359537 184'

   #
   printf '#
      digmsg create 1
      digmsg 1 epoch
      read es
      echo "!=$! ?=$? es<$es>"
      digmsg remove 1
      ' | ${MAILX} ${ARGS} -Rf eml://./t6-7.eml >./t7 2>&1
   check 7 0 ./t7 '3727574170 28'

   </dev/null ${MAILX} ${ARGS} -Y '#
         digmsg create 1
         digmsg 1 epoch
         read es
         echo "!=$! ?=$? es<$es>"
         digmsg remove 1
         xit
      ' -Rf eml://- >./t8 2>&1
   check 8 0 ./t8 '2215033944 46'

   #
   </dev/null ${MAILX} ${ARGS} -Y '#
         commandalias x \echo '"'"'--- $?/$^ERRNAME, '"'"'
         echo ==1
         digmsg create 1 -;x 1
         digmsg 1 header list
         headerpick create t;x 2
         headerpick t ignore date subject to;x 3
         digmsg 1 header headerpick t
         digmsg 1 header list
         digmsg remove 1
         echo ==2
         digmsg create 1 -;x 10
         digmsg 1 header list
         unheaderpick t ignore *;x 12
         headerpick t retain date subject to;x 13
         digmsg 1 header headerpick t
         digmsg 1 header list
         digmsg remove 1
         xit
      ' -f t6-7.eml >./t9 2>&1
   check 9 0 ./t9 '3321043850 245'

   t_epilog "${@}"
} # }}}

t_on_main_loop_tick() { # {{{
   t_prolog "${@}"

   if have_feat cmd-vexpr; then :; else
      t_echoskip '[!CMD_VEXPR]'
      t_epilog "${@}"
      return
   fi

   printf '#
   echo hello; set i=1
define bla {
   echo bla1
   echo bla2
}
define omlt {
   echo in omlt: $i
   vput vexpr i + 1 $i
}
   echo one
   set on-main-loop-tick=omlt
   echo two
   echo three
   echo calling bla;call bla
   echo four
   echo --bye;xit' |
      ${MAILX} ${ARGS} -Smta=test://t1-x -Sescape=! >./t1 2>&1
   check 1 0 ./t1 '3697651500 130'
   [ -f ./t1-x ]; check_exn0 2

   t_epilog "${@}"
} # }}}

t_on_program_exit() { # {{{
   t_prolog "${@}"

   ${MAILX} ${ARGS} \
      -X 'define x {' -X 'echo jay' -X '}' -X x -Son-program-exit=x \
      > ./t1 2>&1
   check 1 0 ./t1 '2820891503 4'

   ${MAILX} ${ARGS} \
      -X 'define x {' -X 'echo jay' -X '}' -X q -Son-program-exit=x \
      > ./t2 2>&1
   check 2 0 ./t2 '2820891503 4'

   </dev/null ${MAILX} ${ARGS} \
      -X 'define x {' -X 'echo jay' -X '}' -Son-program-exit=x \
      > ./t3 2>&1
   check 3 0 ./t3 '2820891503 4'

   </dev/null ${MAILX} ${ARGS} -Smta=test://t5 \
      -X 'define x {' -X 'echo jay' -X '}' -Son-program-exit=x \
      -s subject -. hey@you \
      > ./t4 2>&1
   check 4 0 ./t4 '2820891503 4'
   check 5 - ./t5 '561900352 118'

   t_epilog "${@}"
} # }}}
# }}}

# Heavy use of/rely on state machine (behaviour) and basics {{{
t_compose_hooks() { # {{{ TODO monster
   t_prolog "${@}"

   if have_feat uistrings &&
         have_feat cmd-csop && have_feat cmd-vexpr; then :; else
      t_echoskip '[!UISTRINGS/!CMD_CSOP/!CMD_VEXPR]'
      t_epilog "${@}"
      return
   fi

   { echo line one&&echo line two&&echo line three; } > ./t.readctl
   { echo echo four&&echo echo five&&echo echo six; } > ./t.attach

   #{{{ Supposed to extend t_compose_edits with ~^ stress tests!
   ${cat} <<'__EOT__' > ./t.rc
   define bail {
      echoerr "Failed: $1.  Bailing out"; echo "~x"; xit
   }
   define xerr {
      vput csop es substr "$1" 0 1
      if "$es" != 2; xcall bail "$2: $1"; end
   }
   define read_mline_res {
      read hl; set len=$? es=$! en=$^ERRNAME;\
         echo $len/$es/$^ERRNAME: $hl
      if $es -ne $^ERR-NONE; xcall bail read_mline_res
      elif $len -ne 0; \xcall read_mline_res
      end
   }
   define ins_addr {
      set xh=$1
      echo "~^header list"; read hl; echo $hl;\
         call xerr "$hl" "in_addr ($xh) 0-1"

      echo "~^header insert $xh 'diet <$xh@exam.ple> spliced'";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 1-1"
      echo "~^header insert $xh <${xh}2@exam.ple>";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 1-2"
      echo "~^header insert $xh ${xh}3@exam.ple";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 1-3"
      echo "~^header list $xh"; read hl; echo $hl;\
         call xerr "$hl" "ins_addr $xh 1-4"
      echo "~^header show $xh"; read es; call xerr $es "ins_addr $xh 1-5"
      call read_mline_res

      if "$t_remove" == ""; return; end

      echo "~^header remove $xh"; read es; call xerr $es "ins_addr $xh 2-1"
      echo "~^header remove $xh"; read es; vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 2-2"; end
      echo "~^header list $xh"; read es; vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 2-3"; end
      echo "~^header show $xh"; read es; vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 2-4"; end

      #
      echo "~^header insert $xh 'diet <$xh@exam.ple> spliced'";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 3-1"
      echo "~^header insert $xh <${xh}2@exam.ple>";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 3-2"
      echo "~^header insert $xh ${xh}3@exam.ple";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 3-3"
      echo "~^header list $xh"; read hl; echo $hl;\
         call xerr "$hl" "ins_addr $xh 3-4"
      echo "~^header show $xh"; read es; call xerr $es "ins_addr $xh 3-5"
      call read_mline_res

      echo "~^header remove-at $xh 1"; read es;\
         call xerr $es "ins_addr $xh 3-6"
      echo "~^header remove-at $xh 1"; read es;\
         call xerr $es "ins_addr $xh 3-7"
      echo "~^header remove-at $xh 1"; read es;\
         call xerr $es "ins_addr $xh 3-8"
      echo "~^header remove-at $xh 1"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 3-9"; end
      echo "~^header remove-at $xh T"; read es;\
         vput csop es substr $es 0 3
      if $es != 505; xcall bail "ins_addr $xh 3-10"; end
      echo "~^header list $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 3-11"; end
      echo "~^header show $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 3-12"; end

      #
      echo "~^header insert $xh 'diet <$xh@exam.ple> spliced'";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 4-1"
    echo "~^header insert $xh <${xh}2@exam.ple>\ (comment)\ \\\"Quot(e)d\\\"";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 4-2"
      echo "~^header insert $xh ${xh}3@exam.ple";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 4-3"
      echo "~^header list $xh"; read hl; echo $hl;\
         call xerr "$hl" "header list $xh 3-4"
      echo "~^header show $xh"; read es; call xerr $es "ins_addr $xh 4-5"
      call read_mline_res

      echo "~^header remove-at $xh 3"; read es;\
         call xerr $es "ins_addr $xh 4-6"
      echo "~^header remove-at $xh 2"; read es;\
         call xerr $es "ins_addr $xh 4-7"
      echo "~^header remove-at $xh 1"; read es;\
         call xerr $es "ins_addr $xh 4-8"
      echo "~^header remove-at $xh 1"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 4-9"; end
      echo "~^header remove-at $xh T"; read es;\
         vput csop es substr $es 0 3
      if $es != 505; xcall bail "ins_addr $xh 4-10"; end
      echo "~^header list $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 4-11"; end
      echo "~^header show $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_addr $xh 4-12"; end
   }
   define ins_ref {
      set xh=$1 mult=$2
      echo "~^header list"; read hl; echo $hl;\
         call xerr "$hl" "ins_ref ($xh) 0-1"

      echo "~^header insert $xh <$xh@exam.ple>";\
         read es; echo $es; call xerr "$es" "ins_ref $xh 1-1"
      if $mult -ne 0
         echo "~^header insert $xh <${xh}2@exam.ple>";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 1-2"
         echo "~^header insert $xh ${xh}3@exam.ple";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 1-3"
      else
         echo "~^header insert $xh <${xh}2@exam.ple>"; read es;\
            vput csop es substr $es 0 3
         if $es != 506; xcall bail "ins_ref $xh 1-4"; end
      end

      echo "~^header list $xh"; read hl; echo $hl;\
         call xerr "$hl" "ins_ref $xh 1-5"
      echo "~^header show $xh"; read es; call xerr $es "ins_ref $xh 1-6"
      call read_mline_res

      if "$t_remove" == ""; return; end

      echo "~^header remove $xh"; read es;\
         call xerr $es "ins_ref $xh 2-1"
      echo "~^header remove $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_ref $xh 2-2"; end
      echo "~^header list $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "$es ins_ref $xh 2-3"; end
      echo "~^header show $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_ref $xh 2-4"; end

      #
      echo "~^header insert $xh <$xh@exam.ple>";\
         read es; echo $es; call xerr "$es" "ins_ref $xh 3-1"
      if $mult -ne 0
         echo "~^header insert $xh <${xh}2@exam.ple>";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 3-2"
         echo "~^header insert $xh ${xh}3@exam.ple";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 3-3"
      end
      echo "~^header list $xh";\
         read hl; echo $hl; call xerr "$hl" "ins_ref $xh 3-4"
      echo "~^header show $xh";\
         read es; call xerr $es "ins_ref $xh 3-5"
      call read_mline_res

      echo "~^header remove-at $xh 1"; read es;\
         call xerr $es "ins_ref $xh 3-6"
      if $mult -ne 0 && $xh != subject
         echo "~^header remove-at $xh 1"; read es;\
            call xerr $es "ins_ref $xh 3-7"
         echo "~^header remove-at $xh 1"; read es;\
            call xerr $es "ins_ref $xh 3-8"
      end
      echo "~^header remove-at $xh 1"; read es;\
         vput csop es substr $es 0 3
      if $es != 501;;; xcall bail "ins_ref $xh 3-9"; end
      echo "~^header remove-at $xh T"; read es;\
         vput csop es substr $es 0 3
      if $es != 505; xcall bail "ins_ref $xh 3-10"; end
      echo "~^header show $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_ref $xh 3-11"; end

      #
      echo "~^header insert $xh <$xh@exam.ple> ";\
         read es; echo $es; call xerr "$es" "ins_ref $xh 4-1"
      if $mult -ne 0
         echo "~^header insert $xh <${xh}2@exam.ple> ";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 4-2"
         echo "~^header insert $xh ${xh}3@exam.ple";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 4-3"
      end
      echo "~^header list $xh"; read hl; echo $hl;\
         call xerr "$hl" "ins_ref $xh 4-4"
      echo "~^header show $xh"; read es; call xerr $es "ins_ref $xh 4-5"
      call read_mline_res

      if $mult -ne 0 && $xh != subject
         echo "~^header remove-at $xh 3"; read es;\
            call xerr $es "ins_ref $xh 4-6"
         echo "~^header remove-at $xh 2"; read es;\
            call xerr $es "ins_ref $xh 4-7"
      end
      echo "~^header remove-at $xh 1"; read es;\
         call xerr $es "ins_ref $xh 4-8"
      echo "~^header remove-at $xh 1"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_ref $xh 4-9"; end
      echo "~^header remove-at $xh T"; read es;\
         vput csop es substr $es 0 3
      if $es != 505; xcall bail "ins_ref $xh 4-10"; end
      echo "~^header show $xh"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "ins_ref $xh 4-11"; end
   }
   define t_header {
      echo t_header ENTER
      # In collect.c order
      call ins_addr from
      call ins_ref sender 0 # Not a "ref", but works
      call ins_addr To
      call ins_addr cC
      call ins_addr bCc
      call ins_addr reply-To
      call ins_addr mail-Followup-to
      call ins_ref messAge-id 0
      call ins_ref rEfErEncEs 1
      call ins_ref in-Reply-to 1
      call ins_ref subject 1 # Not a "ref", but works (with tweaks)
      call ins_addr freeForm1
      call ins_addr freeform2

      echo "~^header show MAILX-Command"; read es; call xerr $es "t_header 1000"
      call read_mline_res
      echo "~^header show MAILX-raw-TO"; read es; call xerr $es "t_header 1001"
      call read_mline_res

      echo t_header LEAVE
   }
   define t_attach {
      echo t_attach ENTER

      echo "~^attachment";\
         read hl; echo $hl; vput csop es substr "$hl" 0 3
      if "$es" != 501; xcall bail "attach 0-1"; end

      echo "~^attach attribute ./t.readctl";\
         read hl; echo $hl; vput csop es substr "$hl" 0 3
      if "$es" != 501; xcall bail "attach 0-2"; end
      echo "~^attachment attribute-at 1";\
         read hl; echo $hl; vput csop es substr "$hl" 0 3
      if "$es" != 501; xcall bail "attach 0-3"; end

      echo "~^attachment insert ./t.readctl=ascii";\
         read hl; echo $hl; call xerr "$hl" "attach 1-1"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 1-2"
      call read_mline_res
      echo "~^attachment attribute ./t.readctl";\
         read es; echo $es;call xerr "$es" "attach 1-3"
      call read_mline_res
      echo "~^attachment attribute t.readctl";\
         read es; echo $es;call xerr "$es" "attach 1-4"
      call read_mline_res
      echo "~^attachment attribute-at 1";\
         read es; echo $es;call xerr "$es" "attach 1-5"
      call read_mline_res

      echo "~^attachment attribute-set ./t.readctl filename rctl";\
         read es; echo $es;call xerr "$es" "attach 1-6"
      echo "~^attachment attribute-set t.readctl content-description Au";\
         read es; echo $es;call xerr "$es" "attach 1-7"
      echo "~^attachment attribute-set-at 1 content-id <10.du@ich>";\
         read es; echo $es;call xerr "$es" "attach 1-8"

      echo "~^attachment attribute ./t.readctl";\
         read es; echo $es;call xerr "$es" "attach 1-9"
      call read_mline_res
      echo "~^attachment attribute t.readctl";\
         read es; echo $es;call xerr "$es" "attach 1-10"
      call read_mline_res
      echo "~^attachment attribute rctl";\
         read es; echo $es;call xerr "$es" "attach 1-11"
      call read_mline_res
      echo "~^attachment attribute-at 1";\
         read es; echo $es;call xerr "$es" "attach 1-12"
      call read_mline_res

      #
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 2-1"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 2-2"
      call read_mline_res
      echo "~^attachment attribute ./t.attach";\
         read es; echo $es;call xerr "$es" "attach 2-3"
      call read_mline_res
      echo "~^attachment attribute t.attach";\
         read es; echo $es;call xerr "$es" "attach 2-4"
      call read_mline_res
      echo "~^attachment attribute-at 2";\
         read es; echo $es;call xerr "$es" "attach 2-5"
      call read_mline_res

      echo "~^attachment attribute-set ./t.attach filename tat";\
         read es; echo $es;call xerr "$es" "attach 2-6"
      echo \
      "~^attachment attribute-set t.attach content-description Au2";\
         read es; echo $es;call xerr "$es" "attach 2-7"
      echo "~^attachment attribute-set-at 2 content-id <20.du@wir>";\
         read es; echo $es;call xerr "$es" "attach 2-8"
      echo \
         "~^attachment attribute-set-at 2 content-type application/x-sh";\
        read es; echo $es;call xerr "$es" "attach 2-9"

      echo "~^attachment attribute ./t.attach";\
         read es; echo $es;call xerr "$es" "attach 2-10"
      call read_mline_res
      echo "~^attachment attribute t.attach";\
         read es; echo $es;call xerr "$es" "attach 2-11"
      call read_mline_res
      echo "~^attachment attribute tat";\
         read es; echo $es;call xerr "$es" "attach 2-12"
      call read_mline_res
      echo "~^attachment attribute-at 2";\
         read es; echo $es;call xerr "$es" "attach 2-13"
      call read_mline_res

      #
      if "$t_remove" == ""; return; end

      echo "~^attachment remove ./t.readctl"; read es;\
         call xerr $es "attach 3-1"
      echo "~^attachment remove ./t.attach"; read es;\
         call xerr $es "attach 3-2"
      echo "~^   attachment     remove     ./t.readctl"; read es;\
         vput csop es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 3-3"
      end
      echo "~^   attachment     remove     ./t.attach"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 3-4"; end
      echo "~^attachment list"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 3-5"; end

      #
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 4-1"
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 4-2"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 4-3"
      call read_mline_res
      echo "~^   attachment     remove     t.attach"; read es;\
         vput csop es substr $es 0 3
      if $es != 506; xcall bail "attach 4-4 $es"; end
      echo "~^attachment remove-at T"; read es;\
         vput csop es substr $es 0 3
      if $es != 505; xcall bail "attach 4-5"; end
      echo "~^attachment remove ./t.attach"; read es;\
         call xerr $es "attach 4-6"
      echo "~^attachment remove ./t.attach"; read es;\
         call xerr $es "attach 4-7"
      echo "~^   attachment     remove     ./t.attach"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 4-8 $es"; end
      echo "~^attachment list"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 4-9"; end

      #
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 5-1"
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 5-2"
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 5-3"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 5-4"
      call read_mline_res

      echo "~^attachment remove-at 3"; read es;\
         call xerr $es "attach 5-5"
      echo "~^attachment remove-at 3"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 5-6"; end
      echo "~^attachment remove-at 2"; read es;\
         call xerr $es "attach 5-7"
      echo "~^attachment remove-at 2"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 5-8"
      end
      echo "~^attachment remove-at 1"; read es;\
         call xerr $es "attach 5-9"
      echo "~^attachment remove-at 1"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 5-10"; end

      echo "~^attachment list"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 5-11"; end

      #
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 6-1"
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 6-2"
      echo "~^attachment insert ./t.attach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 6-3"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 6-4"
      call read_mline_res

      echo "~^attachment remove-at 1"; read es;\
         call xerr $es "attach 6-5"
      echo "~^attachment remove-at 1"; read es;\
         call xerr $es "attach 6-6"
      echo "~^attachment remove-at 1"; read es;\
         call xerr $es "attach 6-7"
      echo "~^attachment remove-at 1"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 6-8"; end

      echo "~^attachment list"; read es;\
         vput csop es substr $es 0 3
      if $es != 501; xcall bail "attach 6-9"; end

      echo t_attach LEAVE
   }
   define t_ocs {
      read ver
      echo t_ocs
      call t_header
      call t_attach
   }
   define t_oce {
      echo on-compose-enter
      alternates alter1@exam.ple alter2@exam.ple
      alternates
      set autocc='alter1@exam.ple alter2@exam.ple'
      digmsg create - -;echo $?/$!/$^ERRNAME;\
         digmsg - header list;\
         digmsg - header show mailX-command;\
         digmsg - header show sUbject;\
         digmsg - header show tO;\
         digmsg - header show Cc;\
         digmsg - header show bCc;\
         digmsg - header show Mailx-raw-to;\
         digmsg - header show mailX-raw-cc;\
         digmsg - header show maiLx-raw-bcc;\
         digmsg - header show maIlx-orig-sender;\
         digmsg - header show mAilx-orig-from;\
         digmsg - header show mailx-orig-tO;\
         digmsg - header show mailx-orig-Cc;\
         digmsg - header show mailx-oriG-bcc;\
         digmsg remove -;echo $?/$!/$^ERRNAME
      digmsg create -;echo $?/$!/$^ERRNAME;\
         digmsg - header list;readall x;echon $x;\
         digmsg remove -;echo $?/$!/$^ERRNAME
   }
   define t_ocl {
      echo on-compose-leave
      vput alternates al
      eval alternates $al alter3@exam.ple alter4@exam.ple
      alternates
      set autobcc='alter3@exam.ple alter4@exam.ple'
      digmsg create - -;echo $?/$!/$^ERRNAME;\
         digmsg - header list;\
         digmsg - header show mailX-command;\
         digmsg - header show sUbject;\
         digmsg - header show tO;\
         digmsg - header show Cc;\
         digmsg - header show bCc;\
         digmsg - header show Mailx-raw-to;\
         digmsg - header show mailX-raw-cc;\
         digmsg - header show maiLx-raw-bcc;\
         digmsg - header show maIlx-orig-sender;\
         digmsg - header show mAilx-orig-from;\
         digmsg - header show mailx-orig-tO;\
         digmsg - header show mailx-orig-Cc;\
         digmsg - header show mailx-oriG-bcc;\
         digmsg remove -;echo $?/$!/$^ERRNAME
      digmsg create -;echo $?/$!/$^ERRNAME;\
         digmsg - header list;readall x;echon $x;\
         digmsg remove -;echo $?/$!/$^ERRNAME
   }
   define t_occ {
      echo on-compose-cleanup
      unalternates *
      alternates
      # XXX error message variable digmsg create - -;echo $?/$!/$^ERRNAME;\
         digmsg - header list;\
         digmsg - header show mailX-command;\
         digmsg - header show sUbject;\
         digmsg - header show tO;\
         digmsg - header show Cc;\
         digmsg - header show bCc;\
         digmsg - header show Mailx-raw-to;\
         digmsg - header show mailX-raw-cc;\
         digmsg - header show maiLx-raw-bcc;\
         digmsg - header show maIlx-orig-sender;\
         digmsg - header show mAilx-orig-from;\
         digmsg - header show mailx-orig-tO;\
         digmsg - header show mailx-orig-Cc;\
         digmsg - header show mailx-oriG-bcc;\
         digmsg remove -;echo $?/$!/$^ERRNAME
      # ditto digmsg create -;echo $?/$!/$^ERRNAME;\
         digmsg - header list;readall x;echon $x;\
         digmsg remove -;echo $?/$!/$^ERRNAME
   }
   set on-compose-splice=t_ocs \
      on-compose-enter=t_oce on-compose-leave=t_ocl \
         on-compose-cleanup=t_occ
__EOT__
   #}}}

   printf 'm this-goes@nowhere\nbody\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sstealthmua=noagent \
      -X'source ./t.rc' -Smta=test://t1 \
      >./t1-x 2>&1
   ${cat} ./t1-x >> ./t1
   check 1 0 ./t1 '429858159 10361'

   printf 'm this-goes@nowhere\nbody\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sstealthmua=noagent \
      -St_remove=1 -X'source ./t.rc' -Smta=test://t2 \
      >./t2-x 2>&1
   ${cat} ./t2-x >> ./t2
   check 2 0 ./t2 '2981091690 12672'

   ##

   # Some state machine stress, shell compose hook, localopts for hook, etc.
   # readctl in child. ~r as HERE document
   #{{{
   printf 'local mail ex@am.ple\nbody\n!.
      varshow t_oce t_ocs t_ocs_sh t_ocl t_occ autocc
   ' | ${MAILX} ${ARGS} -Snomemdebug -Sescape=! \
      -Smta=test://t3 \
      -X'
         define bail {
            echoerr "Failed: $1.  Bailing out"; echo "~x"; xit
         }
         define xerr {
            vput csop es substr "$1" 0 1
            if "$es" != 2; xcall bail "$2"; end
         }
         define read_mline_res {
            read hl; set len=$? es=$! en=$^ERRNAME;\
               echo $len/$es/$^ERRNAME: $hl
            if $es -ne $^ERR-NONE; xcall bail read_mline_res
            elif $len -ne 0; \xcall read_mline_res
            end
         }
         define _work {
            vput vexpr i + 1 "$2"
            if $i -lt 111
               vput vexpr j % $i 10
               if $j -ne 0
                  set j=xcall
               else
                  echon "$i.. "
                  set j=call
               end
               eval \\$j _work $1 $i
               return $?
            end
            vput vexpr i + $i "$1"
            return $i
         }
         define _read {
            set line; read line;set es=$? en=$^ERRNAME ;\
               echo read:$es/$en: $line
            if ${es} -ne -1; xcall _read; end
            readctl remove $cwd/t.readctl; echo readctl remove:$?/$^ERRNAME
         }
         define t_ocs {
            read ver
            echo t_ocs
            echo "~^header list"; read hl; echo $hl;\
               vput csop es substr "$hl" 0 1
            if "$es" != 2; xcall bail "header list"; endif
            #
            call _work 1; echo $?
           echo "~^header insert cc splicy\ diet\ <splice@exam.ple>\ spliced";\
               read es; echo $es; vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be diet"; endif
            echo "~^header insert cc <splice2@exam.ple>";\
               read es; echo $es; vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be diet2"; endif
            #
            call _work 2; echo $?
           echo "~^header insert bcc juicy\ juice\ <juice@exam.ple>\ spliced";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be juicy"; endif
            echo "~^header insert bcc juice2@exam.ple";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be juicy2";endif
            echo "~^header insert bcc juice3\ <juice3@exam.ple>";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be juicy3"; endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be juicy4"; endif
            #
            echo "~^header remove-at bcc 3";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "remove juicy5"; endif
            echo "~^header remove-at bcc 2";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "remove juicy6"; endif
            echo "~^header remove-at bcc 3";\
               read es; echo $es;vput csop es substr "$es" 0 3
            if "$es" != 501; xcall bail "failed to remove-at"; endif
            # Add duplicates which ought to be removed!
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be juicy4-1"; endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be juicy4-2"; endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput csop es substr "$es" 0 1
            if "$es" != 2; xcall bail "be juicy4-3"; endif
            echo "~:set t_ocs"

            #
            call _work 3; echo $?
            echo "~r - '__EOT'"
            vput ! i echo just knock if you can hear me;\
               i=0;\
               while [ $i -lt 24 ]; do printf "%s " $i; i=`expr $i + 1`; done;\
               echo relax
            echon shell-cmd says $?/$^ERRNAME: $i
            echo "~x  will not become interpreted, we are reading until __EOT"
            echo "__EOT"
            read r_status; echo "~~r status output: $r_status"
            echo "~:echo $? $! $^ERRNAME"
            read r_status
            echo "~~r status from parent: $r_status"

            #
            call _work 4; echo $?
            vput cwd cwd;echo cwd:$?
            readctl create $cwd/t.readctl     ;echo readctl:$?/$^ERRNAME;\
            call _read

            #
            call _work 5; echo $?
            echo "~^header show MAILX-Command"; read es;\
               call xerr $es "t_header 1000"; call read_mline_res
            echo "~^header show MAILX-raw-TO"; read es;\
               call xerr $es "t_header 1001"; xcall read_mline_res

            echoerr IT IS WRONG IF YOU SEE THIS
         }
         define t_oce {
            echo on-compose-enter
            set t_oce autobcc=oce@exam.ple
            alternates alter1@exam.ple alter2@exam.ple
            alternates
            digmsg create - -;echo $?/$!/$^ERRNAME;\
               digmsg - header list;\
               digmsg - header show mailX-command;\
               digmsg - header show sUbject;\
               digmsg - header show tO;\
               digmsg - header show Cc;\
               digmsg - header show bCc;\
               digmsg - header show Mailx-raw-to;\
               digmsg - header show mailX-raw-cc;\
               digmsg - header show maiLx-raw-bcc;\
               digmsg - header show maIlx-orig-sender;\
               digmsg - header show mAilx-orig-from;\
               digmsg - header show mailx-orig-tO;\
               digmsg - header show mailx-orig-Cc;\
               digmsg - header show mailx-oriG-bcc;\
               digmsg remove -;echo $?/$!/$^ERRNAME
         }
         define t_ocl {
            echo on-compose-leave
            set t_ocl autocc=ocl@exam.ple
            unalternates *
            alternates alter3@exam.ple alter4@exam.ple
            alternates
            digmsg create - -;echo $?/$!/$^ERRNAME;\
               digmsg - header list;\
               digmsg - header show mailX-command;\
               digmsg - header show sUbject;\
               digmsg - header show tO;\
               digmsg - header show Cc;\
               digmsg - header show bCc;\
               digmsg - header show Mailx-raw-to;\
               digmsg - header show mailX-raw-cc;\
               digmsg - header show maiLx-raw-bcc;\
               digmsg - header show maIlx-orig-sender;\
               digmsg - header show mAilx-orig-from;\
               digmsg - header show mailx-orig-tO;\
               digmsg - header show mailx-orig-Cc;\
               digmsg - header show mailx-oriG-bcc;\
               digmsg remove -;echo $?/$!/$^ERRNAME
         }
         define t_occ {
            echo on-compose-cleanup
            set t_occ autocc=occ@exam.ple
            unalternates *
            alternates
            # XXX error message digmsg create - -;echo $?/$!/$^ERRNAME;\
               digmsg - header list;\
               digmsg - header show mailX-command;\
               digmsg - header show sUbject;\
               digmsg - header show tO;\
               digmsg - header show Cc;\
               digmsg - header show bCc;\
               digmsg - header show Mailx-raw-to;\
               digmsg - header show mailX-raw-cc;\
               digmsg - header show maiLx-raw-bcc;\
               digmsg - header show maIlx-orig-sender;\
               digmsg - header show mAilx-orig-from;\
               digmsg - header show mailx-orig-tO;\
               digmsg - header show mailx-orig-Cc;\
               digmsg - header show mailx-oriG-bcc;\
               digmsg remove -;echo $?/$!/$^ERRNAME
         }
         set on-compose-splice=t_ocs \
            on-compose-splice-shell="read ver;echo t_ocs-shell;\
               echo \"~t shell@exam.ple\"; echo \"~:set t_ocs_sh\"" \
            on-compose-enter=t_oce on-compose-leave=t_ocl \
            on-compose-cleanup=t_occ
      ' > ./t3-x 2>&1
   #}}}
   check_ex0 3-estat
   ${cat} ./t3-x >> ./t3
   check 3 - ./t3 '3524594623 2348'

   # Reply, forward, resend, Resend

   printf '#
      set from="f1@z"
      m t1@z
b1
!.
      set from="du <f2@z>" stealthmua=noagent
      m t2@z
b2
!.
      ' | ${MAILX} ${ARGS} -Smta=test://t4 -Snomemdebug -Sescape=! \
         > ./t4-x 2>&1
   check_ex0 4-intro-estat

   #{{{
   printf '
      echo start: $? $! $^ERRNAME
      File ./t4
      echo File: $? $! $^ERRNAME;echo;echo
      reply 1
this is content of reply 1
!.
      echo reply 1: $? $! $^ERRNAME;echo;echo
      Reply 1 2
this is content of Reply 1 2
!.
      echo Reply 1 2: $? $! $^ERRNAME;echo;echo
      forward 1 fwdex@am.ple
this is content of forward 1
!.
      echo forward 1: $? $! $^ERRNAME;echo;echo
      set forward-inject-head=$'"'"'-- \\
         forward (%%a)(%%d)(%%f)(%%i)(%%n)(%%r) --\\n'"'"'
      set forward-inject-tail=$'"'"'-- \\
         end of forward (%%i) --\\n'"'"'
      forward 2 fwdex@am.ple
this is content of forward 2
!.
      echo forward 2: $? $! $^ERRNAME;echo;echo
      set showname
      forward 2 fwdex2@am.ple
this is content of forward 2, 2nd, with showname set
!.
      echo forward 2, 2nd: $? $! $^ERRNAME;echo;echo
      resend 1 2 resendex@am.ple
      echo resend 1 2: $? $! $^ERRNAME;echo;echo
      Resend 1 2 Resendex@am.ple
      echo Resend 1 2: $? $! $^ERRNAME;echo;echo
   ' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sfullnames \
      -Smta=test://t4 \
      -X'
         define bail {
            echoerr "Failed: $1.  Bailing out"; echo "~x"; xit
         }
         define xerr {
            vput csop es substr "$1" 0 1
            if "$es" != 2; xcall bail "$2"; end
         }
         define read_mline_res {
            read hl; set len=$? es=$! en=$^ERRNAME;\
               echo \ \ mline_res:$len/$es/$^ERRNAME: $hl
            if $es -ne $^ERR-NONE
               xcall bail read_mline_res
            elif $len -ne 0
               \xcall read_mline_res
            end
         }
         define work_hl {
            echo "~^header show $1"; read es;\
               call xerr $es "work_hl $1"; echo $1" ->"; call read_mline_res
            if $# -gt 1
               shift
               xcall work_hl "$@"
            end
         }
         define t_ocs {
            read ver
            echo t_ocs version $ver
            echo "~^header list"; read hl; echo $hl;\
            echoerr the header list is $hl;\
               call xerr "$hl" "header list"
            eval vpospar set $hl
            shift
            xcall work_hl "$@"
            echoerr IT IS WRONG IF YOU SEE THIS
         }
         define t_oce {
            echo on-XY-enter
            set t_oce autobcc=oce@exam.ple
            digmsg create - -;echo $?/$!/$^ERRNAME;\
               digmsg - header list;\
               digmsg - header show mailX-command;\
               digmsg - header show sUbject;\
               digmsg - header show tO;\
               digmsg - header show Cc;\
               digmsg - header show bCc;\
               digmsg - header show Mailx-raw-to;\
               digmsg - header show mailX-raw-cc;\
               digmsg - header show maiLx-raw-bcc;\
               digmsg - header show maIlx-orig-sender;\
               digmsg - header show mAilx-orig-from;\
               digmsg - header show mailx-orig-tO;\
               digmsg - header show mailx-orig-Cc;\
               digmsg - header show mailx-oriG-bcc;\
               digmsg remove -;echo $?/$!/$^ERRNAME
         }
         define t_ocl {
            echo on-XY-leave
            set t_ocl autocc=ocl@exam.ple
            digmsg create - -;echo $?/$!/$^ERRNAME;\
               digmsg - header list;\
               digmsg - header show mailX-command;\
               digmsg - header show sUbject;\
               digmsg - header show tO;\
               digmsg - header show Cc;\
               digmsg - header show bCc;\
               digmsg - header show Mailx-raw-to;\
               digmsg - header show mailX-raw-cc;\
               digmsg - header show maiLx-raw-bcc;\
               digmsg - header show maIlx-orig-sender;\
               digmsg - header show mAilx-orig-from;\
               digmsg - header show mailx-orig-tO;\
               digmsg - header show mailx-orig-Cc;\
               digmsg - header show mailx-oriG-bcc;\
               digmsg remove -;echo $?/$!/$^ERRNAME
         }
         define t_occ {
            echo on-XY-cleanup
            set t_occ autocc=occ@exam.ple
            # XXX error message digmsg create - -;echo $?/$!/$^ERRNAME;\
               digmsg - header list;\
               digmsg - header show mailX-command;\
               digmsg - header show sUbject;\
               digmsg - header show tO;\
               digmsg - header show Cc;\
               digmsg - header show bCc;\
               digmsg - header show Mailx-raw-to;\
               digmsg - header show mailX-raw-cc;\
               digmsg - header show maiLx-raw-bcc;\
               digmsg - header show maIlx-orig-sender;\
               digmsg - header show mAilx-orig-from;\
               digmsg - header show mailx-orig-tO;\
               digmsg - header show mailx-orig-Cc;\
               digmsg - header show mailx-oriG-bcc;\
               digmsg remove -;echo $?/$!/$^ERRNAME
         }
         define t_oce_r { # XXX use normal callbacks
            echo on-resend-enter
            set t_oce autobcc=oce@exam.ple
         }
         set on-compose-splice=t_ocs \
            on-compose-enter=t_oce on-compose-leave=t_ocl \
               on-compose-cleanup=t_occ \
            on-resend-enter=t_oce_r on-resend-cleanup=t_occ
      ' >> ./t4-x 2>&1
   #}}}
   check_ex0 4-estat
   ${cat} ./t4-x >> ./t4
   check 4 - ./t4 '3526058127 10502'

   t_epilog "${@}"
} # }}}

t_mass_recipients() { # {{{
   t_prolog "${@}"

   if have_feat cmd-vexpr; then :; else
      t_echoskip '[!CMD_VEXPR]'
      t_epilog "${@}"
      return
   fi

   #{{{
   ${cat} <<'__EOT__' > ./t.rc
   define bail {
      echoerr "Failed: $1.  Bailing out"; echo "~x"; xit
   }
   define ins_addr {
      set nr=$1 hn=$2
      echo "~$hn $hn$nr@$hn"; echo '~:echo $?'; read es
      if "$es" -ne 0; xcall bail "ins_addr $hn 1-$nr"; end
      vput vexpr nr + $nr 1
      if "$nr" -le "$maximum"; xcall ins_addr $nr $hn;  end
   }
   define bld_alter {
      set nr=$1 hn=$2
      alternates $hn$nr@$hn
      vput vexpr nr + $nr 2
      if "$nr" -le "$maximum"; xcall bld_alter $nr $hn; end
   }
   define t_ocs {
      read ver
      call ins_addr 1 t
      call ins_addr 1 c
      call ins_addr 1 b
   }
   define t_ocl {
      if "$t_remove" != ''; call bld_alter 1 t; call bld_alter 2 c; end
   }
   set on-compose-splice=t_ocs on-compose-leave=t_ocl
__EOT__
   #}}}

   printf 'm this-goes@nowhere\nbody\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sstealthmua=noagent \
      -X'source ./t.rc' -Smta=test://t1 -Smaximum=${LOOPS_MAX} \
      >./t1-x 2>&1
   check_ex0 1-estat
   ${cat} ./t1-x >> ./t1
   if [ ${LOOPS_MAX} -eq ${LOOPS_BIG} ]; then
      check 1-${LOOPS_BIG} - ./t1 '3835365533 51534'
   elif [ ${LOOPS_MAX} -eq ${LOOPS_SMALL} ]; then
      check 1-${LOOPS_SMALL} - ./t1 '3647549277 4686'
   fi

   printf 'm this-goes@nowhere\nbody\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sstealthmua=noagent \
      -St_remove=1 -X'source ./t.rc' -Smta=test://t2 \
      -Smaximum=${LOOPS_MAX} \
      >./t2-x 2>&1
   check_ex0 2-estat
   ${cat} ./t2-x >> ./t2
   if [ ${LOOPS_MAX} -eq ${LOOPS_BIG} ]; then
      check 2-${LOOPS_BIG} - ./t2 '3768249992 34402'
   elif [ $LOOPS_MAX -eq ${LOOPS_SMALL} ]; then
      check 2-${LOOPS_SMALL} - ./t2 '4042568441 3170'
   fi

   t_epilog "${@}"
} # }}}

t_lreply_futh_rth_etc() { # {{{
   t_prolog "${@}"

   #{{{
   ${cat} <<-'_EOT' > ./t.mbox
	From neverneverland  Sun Jul 23 13:46:25 2017
	Subject: Bugstop: five miles out 1
	Reply-To: mister originator2 <mr2@originator>, bugstop@five.miles.out
	From: mister originator <mr@originator>
	To: bugstop-commit@five.miles.out, laber@backe.eu
	Cc: is@a.list
	Mail-Followup-To: bugstop@five.miles.out, laber@backe.eu, is@a.list
	In-reply-to: <20170719111113.bkcMz%laber@backe.eu>
	Date: Wed, 19 Jul 2017 09:22:57 -0400
	Message-Id: <20170719132257.766AF781267@originator>
	Status: RO
	
	 >  |Sorry, I think I misunderstand something.  I would think that
	
	That's appalling.
	
	From neverneverland  Fri Jul  7 22:39:11 2017
	Subject: Bugstop: five miles out 2
	Reply-To: mister originator2<mr2@originator>,bugstop@five.miles.out,is@a.list
	Content-Transfer-Encoding: 7bit
	From: mister originator <mr@originator>
	To: bugstop-commit@five.miles.out
	Cc: is@a.list
	Message-ID: <149945963975.28888.6950788126957753723.reportbug@five.miles.out>
	Date: Fri, 07 Jul 2017 16:33:59 -0400
	Status: R
	
	capable of changing back.
	
	From neverneverland  Fri Jul  7 22:42:00 2017
	Subject: Bugstop: five miles out 3
	Reply-To: mister originator2 <mr2@originator>, bugstop@five.miles.out
	Content-Transfer-Encoding: 7bit
	From: mister originator <mr@originator>
	To: bugstop-commit@five.miles.out
	Cc: is@a.list
	Message-ID: <149945963975.28888.6950788126957753746.reportbug@five.miles.out>
	Date: Fri, 07 Jul 2017 16:33:59 -0400
	List-Post: <mailto:bugstop@five.miles.out>
	Status: R
	
	are you ready, boots?
	
	From neverneverland  Sat Aug 19 23:15:00 2017
	Subject: Bugstop: five miles out 4
	Reply-To: mister originator2 <mr2@originator>, bugstop@five.miles.out
	Content-Transfer-Encoding: 7bit
	From: mister originator <mr@originator>
	To: bugstop@five.miles.out
	Cc: is@a.list
	Message-ID: <149945963975.28888.6950788126qtewrqwer.reportbug@five.miles.out>
	Date: Fri, 07 Jul 2017 16:33:59 -0400
	List-Post: <mailto:bugstop@five.miles.out>
	Status: R
	
	are you ready, boots?
	_EOT
   #}}}

   #{{{
   ${cat} <<-'_EOT' | ${MAILX} ${ARGS} -Sescape=! -Smta=test://t1 \
         -Rf ./t.mbox >> ./t1 2>&1
	define r {
	   set m="This is text of \"reply ${1}."
	   reply 1 2 3
	!I m
	1".
	!.
	!I m
	2".
	!.
	!I m
	3".
	!.
	   echo -----After reply $1.1 - $1.3: $?/$^ERRNAME
	}
	define R {
	   set m="This is text of \"Reply ${1}."
	   eval Reply $2
	!I m
	!I 2
	".
	!.
	   echo -----After Reply $1.$2: $?/$^ERRNAME
	}
	define _Lh {
	   read protover
	   echo '~I m'
	   echo '~I n'
	   echo '".'
	}
	define _Ls {
	   set m="This is text of \"Lreply ${1}." on-compose-splice=_Lh n=$2
	   eval Lreply $2
	}
	define L {
	   # We need two indirections for this test: one for the case that Lreply
	   # fails because of missing recipients: we need to read EOF next, thus
	   # place this in _Ls last; and second for the succeeding cases EOF is
	   # not what these should read, so go over the backside and splice it in!
	   # (A shame we do not have redirection++ as a Bourne/K/POSIX shell!)
	   call _Ls "$@"
	   echo -----After Lreply $1.$2: $?/$^ERRNAME
	}
	define x {
	   commandalias lc '\local call'
	   lc r $1
	   lc R $1 1; lc R $1 2; lc R $1 3; lc R $1 4
	   lc L $1 1; lc L $1 2; lc L $1 3
	   uncommandalias lc
	}
	define tweak {
	   echo;echo '===== CHANGING === '"$*"' =====';echo
	   eval "$@"
	}
	#
	set from=laber@backe.eu
	mlist is@a.list
	call x 1
	call tweak set reply-to-honour
	call x 2
	call tweak set followup-to
	call x 3
	call tweak set followup-to-honour
	call x 4
	call tweak mlist bugstop@five.miles.out
	call x 5
	call tweak mlsubscribe bugstop@five.miles.out
	call x 6
	call tweak set recipients-in-cc
	call x 7
	# While here, test that *fullnames* works (also here)
	call tweak set fullnames
	reply 1
	This message should have *fullnames* in the header.
	!.
	# Revert
	call tweak unmlsubscribe bugstop@five.miles.out';' \
		set followup-to-add-cc nofullnames
	call x 8
	call tweak mlsubscribe bugstop@five.miles.out
	call x 9
	_EOT
   #}}}
   check_ex0 1-estat
   if have_feat uistrings; then
      check 1 - ./t1 '3306748615 41875'
   else
      t_echoskip '1:[!UISTRINGS]'
   fi

   #{{{
   ${cat} <<-'_EOT' > ./t.mbox
	From tom@i-i.example Thu Oct 26 03:15:55 2017
	Date: Wed, 25 Oct 2017 21:15:46 -0400
	From: tom <tom@i-i.example>
	To: Steffen Nurpmeso <steffen@sdaoden.eu>
	Cc: tom <tom@i-i.example>
	Subject: Re: xxxx yyyyyyyy configure does not really like a missing zzzzz
	Message-ID: <20171026011546.GA11643@i-i.example>
	Reply-To: tom@i-i.example
	References: <20171025214601.T2pNd%steffen@sdaoden.eu>
	In-Reply-To: <20171025214601.T2pNd%steffen@sdaoden.eu>
	Status: R
	
	The report's useful :-)
	_EOT
   #}}}

   # Let us test In-Reply-To: removal starts a new thread..
   # This needs adjustment of *stealthmua*
   argadd='-Sstealthmua=noagent -Shostname'

   printf 'reply 1\nthread\n!.\n' |
      ${MAILX} ${ARGS} -Sescape=! -Smta=test://t2_11 -Sreply-to-honour \
         ${argadd} -Rf ./t.mbox > ./t3 2>&1
   check 2 0 ./t2_11 '2966409435 480'
   check 3 - ./t3 '4294967295 0'

   printf 'reply 1\nnew <- thread!\n!||%s -e "%s"\n!.\n' \
         "${sed}" '/^In-Reply-To:/d' |
      ${MAILX} ${ARGS} -Sescape=! -Smta=test://t2_11 -Sreply-to-honour \
         ${argadd} -Rf ./t2_11 > ./t5 2>&1
   check 4 0 ./t2_11 '3870393639 865'
   check 5 - ./t5 '4294967295 0'

   printf 'reply 2\nold <- new <- thread!\n!.\n' |
      ${MAILX} ${ARGS} -Sescape=! -Smta=test://t2_11 -Sreply-to-honour \
         ${argadd} -Rf ./t2_11 > ./t7 2>&1
   check 6 0 ./t2_11 '219545266 1372'
   check 7 - ./t7 '4294967295 0'

   printf 'reply 3\nnew <- old <- new <- thread!\n!|| %s -e "%s"\n!.\n' \
         "${sed}" '/^In-Reply-To:/d' |
      ${MAILX} ${ARGS} -Sescape=! -Smta=test://t2_11 -Sreply-to-honour \
         ${argadd} -Rf ./t2_11 > ./t9 2>&1
   check 8 0 ./t2_11 '529088127 1771'
   check 9 - ./t9 '4294967295 0'

   # And follow-up testing whether changing In-Reply-To: to - starts a new
   # thread with only the message being replied-to.

   printf 'reply 1\nthread with only one ref!\n!||%s -e "%s"\n!.\n' \
         "${sed}" 's/^In-Reply-To:.*$/In-Reply-To:-/' |
      ${MAILX} ${ARGS} -Sescape=! -Smta=test://t2_11 -Sreply-to-honour \
         ${argadd} -Rf ./t2_11 > ./t11 2>&1
   check 10 0 ./t2_11 '968429240 2282'
   check 11 - ./t11 '4294967295 0'

   t_epilog "${@}"
} # }}}

t_pipe_handlers() { # {{{
   t_prolog "${@}"

   if have_feat cmd-fop; then :; else
      t_echoskip '[!CMD_FOP]'
      t_epilog "${@}"
      return
   fi

   # "Test for" [d6f316a] (Gavin Troy)
   printf "m ./t1\n~s subject1\nEmail body\n~.\nfi ./t1\np\nx\n" |
   ${MAILX} ${ARGS} ${ADDARG_UNI} -Spipe-text/plain="?* ${cat}" > ./t2
   check 1 0 ./t1 '3942990636 118'
   check 2 - ./t2 '3951695530 170'

   printf "m ./t3_7\n~s subject2\n~@%s\nBody2\n~.\nFi ./t3_7\nmimeview\nx\n" \
         "${TOPDIR}snailmail.jpg" |
      ${MAILX} ${ARGS} ${ADDARG_UNI} \
         -S 'pipe-text/plain=?' \
         -S 'pipe-image/jpeg=?=&?'\
'trap \"'"${rm}"' -f '\ '\\"${MAILX_FILENAME_TEMPORARY}\\"\" EXIT;'\
'trap \"trap \\\"\\\" INT QUIT TERM; exit 1\" INT QUIT TERM;'\
'{ echo C=$MAILX_CONTENT;'\
'echo C-E=$MAILX_CONTENT_EVIDENCE;'\
'echo E-B-U=$MAILX_EXTERNAL_BODY_URL;'\
'echo F=$MAILX_FILENAME;'\
'echo F-G=not testable MAILX_FILENAME_GENERATED;'\
'echo F-T=not testable MAILX_FILENAME_TEMPORARY;'\
''"${cksum}"' < \"${MAILX_FILENAME_TEMPORARY}\" |'\
''"${sed}"' -e "s/[ 	]\{1,\}/ /g"; } > ./t4-x 2>&1;'"${mv}"' ./t4-x ./t4-y' \
            > ./t4 2>&1
   check 3 0 ./t3_7 '1933681911 13435'
   check 4 - ./t4 '2036666633 493'
   check 4-hdl - ./t4-y '144517347 151' async

   # Keep $MBOX..
   if [ -z "${ln}" ]; then
      t_echoskip '5:[ln(1) not found]'
   else
      # Let us fill in tmpfile, test auto-deletion
      printf 'Fi ./t3_7\nmimeview\nvput fop v stat .t5.one-link\n'\
'eval set $v;echo should be $st_nlink link\nx\n' |
         ${MAILX} ${ARGS} ${ADDARG_UNI} \
            -S 'pipe-text/plain=?' \
            -S 'pipe-image/jpeg=?=++?'\
'echo C=$MAILX_CONTENT;'\
'echo C-E=$MAILX_CONTENT_EVIDENCE;'\
'echo E-B-U=$MAILX_EXTERNAL_BODY_URL;'\
'echo F=$MAILX_FILENAME;'\
'echo F-G=not testable MAILX_FILENAME_GENERATED;'\
'echo F-T=not testable MAILX_FILENAME_TEMPORARY;'\
"${ln}"' -f $MAILX_FILENAME_TEMPORARY .t5.one-link;'\
''"${cksum}"' < \"${MAILX_FILENAME_TEMPORARY}\" |'\
''"${sed}"' -e "s/[ 	]\{1,\}/ /g"' \
               > ./t5 2>&1
      check 5 0 ./t5 '4260004050 661'

      # Fill in ourselfs, test auto-deletion
      printf 'Fi ./t3_7\nmimeview\nvput fop v stat .t6.one-link\n'\
'eval set $v;echo should be $st_nlink link\nx\n' |
         ${MAILX} ${ARGS} ${ADDARG_UNI} \
            -S 'pipe-text/plain=?' \
            -S 'pipe-image/jpeg=?++?'\
"${cat}"' > $MAILX_FILENAME_TEMPORARY;'\
'echo C=$MAILX_CONTENT;'\
'echo C-E=$MAILX_CONTENT_EVIDENCE;'\
'echo E-B-U=$MAILX_EXTERNAL_BODY_URL;'\
'echo F=$MAILX_FILENAME;'\
'echo F-G=not testable MAILX_FILENAME_GENERATED;'\
'echo F-T=not testable MAILX_FILENAME_TEMPORARY;'\
"${ln}"' -f $MAILX_FILENAME_TEMPORARY .t6.one-link;'\
''"${cksum}"' < \"${MAILX_FILENAME_TEMPORARY}\" |'\
''"${sed}"' -e "s/[ 	]\{1,\}/ /g"' \
               > ./t6 2>&1
      check 6 0 ./t6 '4260004050 661'

      # And the same, via copiousoutput (fake)
      printf 'Fi ./t3_7\np\nvput fop v stat .t7.one-link\n'\
'eval set $v;echo should be $st_nlink link\nx\n' |
         ${MAILX} ${ARGS} ${ADDARG_UNI} \
            -S 'pipe-text/plain=?' \
            -S 'pipe-image/jpeg=?*++?'\
"${cat}"' > $MAILX_FILENAME_TEMPORARY;'\
'echo C=$MAILX_CONTENT;'\
'echo C-E=$MAILX_CONTENT_EVIDENCE;'\
'echo E-B-U=$MAILX_EXTERNAL_BODY_URL;'\
'echo F=$MAILX_FILENAME;'\
'echo F-G=not testable MAILX_FILENAME_GENERATED;'\
'echo F-T=not testable MAILX_FILENAME_TEMPORARY;'\
"${ln}"' -f $MAILX_FILENAME_TEMPORARY .t7.one-link;'\
''"${cksum}"' < \"${MAILX_FILENAME_TEMPORARY}\" |'\
''"${sed}"' -e "s/[ 	]\{1,\}/ /g"' \
               > ./t7 2>&1
      check 7 0 ./t7 '709946464 677'
   fi

   ## Extension chains, type-markers (note: "linked" by t_mimetype())

   tmt='#
unmimetype *
mimetype application/y-unix-readme README INSTALL TODO COPYING NEWS
mimetype application/y-tar-gz tgz tar.gz
mimetype application/y-ma-tar-gz ma.tar.gz
mimetype application/y-x-ma-tar-gz x.ma.tar.gz
mimetype application/y-fun x.tar
mimetype application/y-xfun x
mimetype application/y-tar  tar
mimetype application/y-gzip  tgz gz emz
mimetype ${x} application/x-unix-readme README INSTALL TODO COPYING NEWS
mimetype ${x} application/x-tar-gz tgz tar.gz
mimetype ${x} application/x-ma-tar-gz ma.tar.gz
mimetype ${x} application/x-x-ma-tar-gz x.ma.tar.gz
mimetype ${x} application/x-fun x.tar
mimetype ${x} application/x-xfun x
mimetype ${x} application/x-tar  tar
mimetype ${x} application/x-gzip  tgz gz emz
'

   tfs='README x.gz x.ma.tar.gz x.tar x.tar.gz '\
'y.x.ma.tar.gz .x .x.ma.tar.gz .x.tar y.x.tar x.x NEWS'

   for f in ${tfs}; do printf 'body '${f}'\n' > ./${f}; done

   printf 'm ./t11\nLine1\n~@ %s\n~.\nxit' "${tfs}" |
      ${MAILX} -S x -Y "${tmt}" ${ARGS} > ./t10 2>&1
   check 10 0 ./t10 '4294967295 0'
   check 11 - ./t11 '3184122137 2390'

   printf 'type\nxit' |
      ${MAILX} -S x -Y "${tmt}" ${ARGS} -Rf ./t11 > ./t12 2>&1
   check 12 0 ./t12 '1151825807 2610'

   # base handler: text
   printf 'type\nxit' |
      ${MAILX} -S x=? -Y "${tmt}" ${ARGS} -Rf ./t11 > ./t13 2>&1
   check 13 0 ./t13 '4188775633 2079'

   # handler-only, text: text
   printf 'type\nxit' |
      ${MAILX} -S x='?*t' -Y "${tmt}" ${ARGS} -Rf ./t11 > ./t14 2>&1
   check 14 0 ./t14 '4188775633 2079'

   # handler-only, no text: unhandled
   printf 'type\nxit' |
      ${MAILX} -S x='?*' -Y "${tmt}" ${ARGS} -Rf ./t11 > ./t15 2>&1
   check 15 0 ./t15 '1151825807 2610'

   # hdl-only type-marker is honoured when sending
   printf 'm ./t21\nLine1\n~@ %s\n~.\nxit' "${tfs}" |
      ${MAILX} -S x='?*' -Y "${tmt}" ${ARGS} > ./t20 2>&1
   check 20 0 ./t20 '4294967295 0'
   check 21 - ./t21 '2035947076 2390'

   printf 'type\nxit' |
      ${MAILX} -S x -Y "${tmt}" ${ARGS} -Rf ./t21 > ./t22 2>&1
   check 22 0 ./t22 '576517884 2610'

   printf 'type\nxit' |
      ${MAILX} -S x=? -Y "${tmt}" ${ARGS} -Rf ./t21 > ./t23 2>&1
   check 23 0 ./t23 '576517884 2610'

   printf 'type\nxit' |
      ${MAILX} -Sx -Y "${tmt}" \
      -Y 'mimetype ?*t application/y-ma-tar-gz ma.tar.gz' \
      -Y 'mimetype ?t* application/x-x-ma-tar-gz x.ma.tar.gz' \
      -Y 'mimetype ?t application/x-unix-readme README' \
      ${ARGS} -Rf ./t21 > ./t24 2>&1
   check 24 0 ./t24 '1515966968 2531'

   # .. and * still needs a handler
   printf 'type\nxit' |
      ${MAILX} -Sx -S mime-counter-evidence=0b0110 -Y "${tmt}" \
      -Y 'mimetype ?*t application/z-ma-tar-gz ma.tar.gz' \
      -Y 'mimetype ?t* application/z-x-ma-tar-gz x.ma.tar.gz' \
      -Y 'mimetype ?t application/z-unix-readme README' \
      -Y 'mimetype ?* application/z-xfun x' \
      -Y 'mimetype ?* application/z-fun x.tar' \
      -S pipe-application/z-fun="?* echo in; ${cat}; echo out" \
      ${ARGS} -Rf ./t21 > ./t25 2>&1
   check 25 0 ./t25 '2423141259 2813'

   t_epilog "${@}"
} # }}}

t_mailcap() { # {{{
   t_prolog "${@}"

   if have_feat mailcap; then :; else
      t_echoskip '[!MAILCAP]'
      t_epilog "${@}"
      return
   fi

   #{{{
   ${cat} <<-'_EOT' > ./t.mailcap
text/html; lynx -dump %s; copiousoutput; nametemplate=%s.html
application/pdf; /Applications/Preview.app/Contents/MacOS/Preview %s;\
  nametemplate=%s.pdf;\
  test = [ "${OSTYPE}" = darwin ]
application/pdf;\
  infile=%s\;\
    trap "rm -f ${infile}" EXIT\;\
    trap "exit 75" INT QUIT TERM\;\
    mupdf "${infile}";\
  test = [ -n "${DISPLAY}" ];\
  nametemplate = %s.pdf; x-mailx-async
application/pdf; pdftotext -layout %s -; nametemplate=%s.pdf; copiousoutput
application/*; echo "This is \\"%t\\" but \
     is 50 \% Greek to me" \; < %s head -c 1024 | cat -vet; \
     description=" this is\;a \"wildcard\" match, no trailing quote!    ;\
   copiousoutput; x-mailx-noquote

    
                                     ;
bummer/hummer;;
application/postscript; ps-to-terminal %s;\ needsterminal
application/postscript; ps-to-terminal %s; \compose=idraw %s
x-be2; the-cmd %s; \
  print=print-cmd %s ; \
            copiousoutput             ;         \
  compose=compose-cmd -d %s ; \
                   textualnewlines;    \
  composetyped = composetyped-cmd -dd %s ; \
    x-mailx-noquote ;\
  edit=edit-cmd -ddd %s; \
  description = a\;desc;\
  nametemplate=%s.be2;\
  test                       =                 this is "a" test ;  \
    x-mailx-test-once ;\
  x11-bitmap = x11-bitmap.bpm;;;;;
application/*; echo "is \"%t\" \
  50 \% Greek" \; cat %s; copiousoutput; \; description="catch-all buddy";
audio/*; showaudio;compose=%n
image/jpeg; showpicture -viewer xv %s
image/*; showpicture %s
message/partial; showpartial %s %{id} %{number} %{total}
application/postscript ; lpr %s ; label="PS File";\
	compose="getx PS %s"
application/atomicmail; atomicmail %s ; needsterminal
application/andrew-inset;     ezview %s ; copiousoutput;\
   edit=ez -d %s; compose="ez -d %s"; label="Andrew i/d"
text/richtext; xy iso-8859-1 -e richtext -p %s; \
   test=test "`echo %{charset} | tr A-Z a-z`"  = iso-8859-1; copiousoutput
text/plain; xy iso-8859-1 %s;\
   test=test "`echo %{charset} | tr A-Z a-z`" = iso-8859-1; copiousoutput
text/richtext; rich %s %{not-closed; copiousoutput
default; cat %s; copiousoutput
_EOT
   ${chmod} 0644 ./t.mailcap
   #}}}

   printf 'm;echo =1/$?;m c;echo =2/$?;
         mailca loa;echo =3/$?;mailc s;echo =4/$?' |
      MAILCAPS=./t.mailcap ${MAILX} -X'commandalias m mailcap' ${ARGS} \
         > ./t1 2>./t2
   check 1 0 ./t1 '2012114724 3064'
   check 2 - ./t2 \
      "`have_feat uistrings && echo '3981551532 2338' || echo '4294967295 0'`"

   ##

   echo 'From me with love' | ${MAILX} ${ARGS} -s sub1 ./t3_7
   check 3 0 ./t3_7 '4224630386 228'

   # For reproducability, one pseudo check with cat(1) and mv(1)
   printf '#
text/plain; echo p-1-1\\;< %%s cat\\;echo p-1-2;\\
      test=echo X >> ./t.errmc\\; [ -n "$XY" ];x-mailx-test-once
text/plain; echo p-2-1\\;< %%s cat\\;echo p-2-2;\\
      test=echo Y >> ./t.errmc\\;[ -z "$XY" ]
text/plain; { file=%%s\\; echo p-3-1 = ${file##*.}\\;\\
         </dev/null cat %%s\\;echo p-3-2\\; } > ./t-x\\; mv -f ./t-x ./t-asy;\\
      test=[ -n "$XY" ];nametemplate=%%s.txt;x-mailx-async
text/plain; echo p-4-1\\;cat\\;echo p-4-2;copiousoutput
   ' > ./t.mailcap

   </dev/null MAILCAPS=./t.mailcap TMPDIR=`${pwd}` \
   ${MAILX} ${ARGS} -Snomailcap-disable \
      -Y '\mailcap' \
      -Rf ./t3_7 > ./t4.virt 2>&1
   check 4.virt 0 ./t4.virt '2992597441 455'

   # Same with real programs
   printf '#
text/plain; echo p-1-1\\;< %%s %s\\;echo p-1-2;\\
      test=echo X >> ./t.errmc\\; [ -n "$XY" ];x-mailx-test-once
text/plain; echo p-2-1\\;< %%s %s\\;echo p-2-2;\\
      test=echo Y >> ./t.errmc\\;[ -z "$XY" ]
text/plain; { file=%%s\\; echo p-3-1 = ${file##*.}\\;\\
         </dev/null %s %%s\\;echo p-3-2\\; } > ./t-x\\; %s -f ./t-x ./t-asy;\\
      test=[ -n "$XY" ];nametemplate=%%s.txt;x-mailx-async
text/plain; echo p-4-1\\;%s\\;echo p-4-2;copiousoutput
   ' "${cat}" "${cat}" "${cat}" "${mv}" "${cat}" > ./t.mailcap

   </dev/null MAILCAPS=./t.mailcap TMPDIR=`${pwd}` \
   ${MAILX} ${ARGS} -Snomailcap-disable \
      -Y '#
\echo =1
\mimeview
\echo =2
\environ set XY=yes
\mimeview
\echo =3
\type
\echo =4
' \
      -Rf ./t3_7 > ./t4 2>./t5
   check 4 0 ./t4 '1912261831 831'
   check 5 - ./t5 '4294967295 0'
   check 6 - ./t.errmc '2376112102 6'
   check 7 - ./t-asy '3913344578 37' async

   # "Binary data"; ensure all possible temporary file / nametemplate
   # etc. paths are taken: avoid 2nd e7a60732c1906aefe4755fd61c5ffa81eeca0af0

   printf 'dubo�om' > ./tatt.pdf
   printf 'du' | ${MAILX} ${ARGS} -a ./tatt.pdf -s test ./t8_9
   check 8 0 ./t8_9 '1092812996 643'

   #{{{
   printf '#
# stdin
application/pdf; echo p-1-1\\;%s\\;echo p-1-2;  test=[ "$XY" = "" ]
# tmpfile, no template
application/pdf; echo p-2-1\\;< %%s %s\\;echo p-2-2;  test  =  [ "$XY" = two ]
# tmpfile, template
application/pdf; echo p-3-1\\;< %%s %s\\;echo p-3-2; test=[ "$XY" = three ];\\
   nametemplate=%%s.txt
# tmpfile, template, async
application/pdf; { file=%%s \\; echo p-4-1 = ${file##*.}\\;\\
         </dev/null %s %%s\\;echo p-4-2\\; } > ./t-x\\; %s -f ./t-x ./t-asy;\\
      test=[ "$XY" = four ]  ; nametemplate  =   %%s.txt  ; x-mailx-async
# copious,stdin
application/pdf; echo p-5-1\\;%s\\;echo p-5-2;  test=[ "$XY" = 1 ];\\
   copiousoutput
# copious, tmpfile, no template
application/pdf; echo p-6-1\\;< %%s %s\\;echo p-6-2;  test = [ "$XY" = 2 ];\\
   copiousoutput
# copious, tmpfile, template
application/pdf; echo p-7-1\\;< %%s %s\\;echo p-7-2;test = [ "$XY" = 3 ];\\
   nametemplate=%%s.txt; copiousoutput
   ' "${cat}" "${cat}" "${cat}" "${cat}" "${mv}" "${cat}" "${cat}" "${cat}" \
   > ./t.mailcap
   #}}}

   #{{{
   </dev/null XY= MAILCAPS=./t.mailcap TMPDIR=`${pwd}` \
   ${MAILX} ${ARGS} -Snomailcap-disable \
      -Y '#
\echo =1
\mimeview
\echo =2
\environ set XY=two
\mimeview
\echo =3
\environ set XY=three
\mimeview
\echo =4
\environ set XY=four
\mimeview
\echo =5
\environ set XY=1
\type
\echo =6
\environ set XY=2
\type
\echo =7
\environ set XY=3
\type
\echo =8
' \
      -Rf ./t8_9 > ./t9 2>./t10
   #}}}
   check 9 0 ./t9 '2191230537 3843'
   check 10 - ./t10 '4294967295 0'
   check 11 - ./t-asy '842146666 27' async

   # x-mailx-last-resort, x-mailx-ignore

   printf 'in a pdf\n' > ./tatt.pdf
   printf 'du\n' | ${MAILX} ${ARGS} -a ./tatt.pdf -s test ./t12_13
   check 12 0 ./t12_13 '1933817004 578'

   printf '#
# stdin
application/pdf;echo hidden;x-mailx-ignore
application/pdf;echo hidden;copiousoutput;x-mailx-ignore
application/pdf; echo pre\\;%s\\;echo post; x-mailx-last-resort
   ' "${cat}" > ./t.mailcap

   #{{{
   </dev/null XY= MAILCAPS=./t.mailcap TMPDIR=`${pwd}` \
   ${MAILX} ${ARGS} -Snomailcap-disable \
      -Y '#
\echo =1
\mimeview
\echo =2
\mimetype ?t application/pdf  pdf
\mimeview
\echo =3
\type
\echo =4
\unmimetype application/pdf
\mimeview
\echo =5
' \
      -Rf ./t12_13 > ./t13 2>./t14
   #}}}
   check 13 0 ./t13 '1548239330 1957'
   check 14 - ./t14 '4294967295 0'

   t_epilog "${@}"
} # }}}
# }}}

# Unclassified rest {{{
t_top() { # {{{
   t_prolog "${@}"

   t__gen_msg subject top1 to 1 from 1 cc 1 body 'body1-1
body1-2

body1-3
body1-4


body1-5
'     > ./t.mbox

   t__gen_msg subject top2 to 1 from 1 cc 1 body 'body2-1
body2-2


body2-3


body2-4
body2-5
'     >> ./t.mbox

   ${MAILX} ${ARGS} -Rf -Y '#
\top 1
\echo --- $?/$^ERRNAME, 1; \set toplines=10
\top 1
\echo --- $?/$^ERRNAME, 2; \set toplines=5
\headerpick top retain subject # For top
\headerpick type retain to subject # For Top
\top 1
\echo --- $?/$^ERRNAME, 3; \set topsqueeze
\top 1 2
\echo --- $?/$^ERRNAME, 4
\Top 1
\echo --- $?/$^ERRNAME, 5
#  ' ./t.mbox > ./t1 2>&1
   check 1 0 ./t1 '2556125754 705'

   t_epilog "${@}"
} # }}}

# xxx Note: t_z() was the first test (series) written.  Today many
# xxx aspects are (better) covered by other tests above, some are not.
# xxx At some future date and time, convert the last remains not covered
# xxx elsewhere to a real t_* test and drop it
t_z() { # {{{
   t_prolog "${@}"

   # Test for [260e19d] (Juergen Daubert)
   echo body | ${MAILX} ${ARGS} ./t1
   check 4 0 ./t1 '2948857341 94'

   # "Test for" [c299c45] (Peter Hofmann) TODO shouldn't end up QP-encoded?
   ${awk} 'BEGIN{
      for(i = 0; i < 10000; ++i)
         printf "\xC3\xBC"
         #printf "\xF0\x90\x87\x90"
      }' | ${MAILX} ${ARGS} ${ADDARG_UNI} -s TestSubject ./t7
   check 7 0 ./t7 '1707496413 61812'

   t_epilog "${@}"
} # }}}
# }}}

# OPT_TLS (basics, like S/MIME) {{{
t_s_mime() { # {{{
   t_prolog "${@}"

   if have_feat smime; then :; else
      t_echoskip '[!SMIME]'
      t_epilog "${@}"
      return
   fi

   if t__tls_certs; then :; else
      t_echoskip '[!TLS certificate setup]'
      t_epilog "${@}"
      return
   fi

   #{{{
   doit() {
      _z=${1}

      if [ "${_z}" = 0 ]; then
         _pass=
         _ossl=
         _f=client
      else
         _pass=client-key-pass
         _osslreq=
         _ossl='-passin pass:'${_pass}
         _f=client-pass
      fi

      _z=`add ${_z} 1`

      # Sign/verify
      echo bla | ${MAILX} ${ARGS} \
         -Ssmime-sign -Ssmime-sign-cert=./${_f}-pair.pem \
         -Sfrom=test@localhost \
         -Ssmime-sign-digest=sha1 \
         -S password-test@localhost.smime-cert-key=${_pass} \
         -s 'S/MIME test' ./t${1}.VERIFY >>${ERR} 2>&1
      check_ex0 ${_z}-estat
      ${awk} '
         BEGIN{ skip=0 }
         /^Content-Description: /{ skip = 2; print; next }
         /^$/{ if(skip) --skip }
         { if(!skip) print }
      ' \
         < ./t${1}.VERIFY > ./t${_z}
      check ${_z} - ./t${_z} '54262178 667'
      _z=`add ${_z} 1`

      printf 'verify\nx\n' |
      ${MAILX} ${ARGS} -Ssmime-ca-file=./ca.pem -Serrexit \
         -R -f ./t${1}.VERIFY >>${ERR} 2>&1
      check_ex0 ${_z} # XXX pipe
      _z=`add ${_z} 1`

      openssl smime -verify -CAfile ./ca.pem \
         -in ./t${1}.VERIFY >>${ERR} 2>&1
      check_ex0 ${_z}
      _z=`add ${_z} 1`

      # (signing +) encryption / decryption
      echo bla |
      ${MAILX} ${ARGS} \
         -Smta=test://t${1}.ENCRYPT \
         -Ssmime-force-encryption \
         -Ssmime-encrypt-recei@ver.com=./client2-cert.pem \
         -Ssmime-sign-digest=sha1 \
         -Ssmime-sign -Ssmime-sign-cert=./${_f}-pair.pem \
         -Sfrom=test@localhost \
         -S password-test@localhost.smime-cert-key=${_pass} \
         -s 'S/MIME test' recei@ver.com >>${ERR} 2>&1
      check_ex0 ${_z}-estat
      ${sed} -e '/^$/,$d' < ./t${1}.ENCRYPT > ./t${_z}
      check ${_z} - ./t${_z} '1324731554 359'
      _z=`add ${_z} 1`

      printf 'decrypt ./t%s.DECRYPT\nfi ./t%s.DECRYPT\nverify\nx\n' \
         "${1}" "${1}" |
      ${MAILX} ${ARGS} \
         -Smta=test://./t${1}.ENCRYPT \
         -Ssmime-ca-file=./ca.pem \
         -Ssmime-sign-cert=./client2-pair.pem \
         -Serrexit -R -f ./t${1}.ENCRYPT >>${ERR} 2>&1
      check_ex0 ${_z}-estat
      ${awk} '
         BEGIN{ skip=0 }
         /^Content-Description: /{ skip = 2; print; next }
         /^$/{ if(skip) --skip }
         { if(!skip) print }
      ' \
         < ./t${1}.DECRYPT > ./t${_z}
      check ${_z} - ./t${_z} '3545588265 963'
      _z=`add ${_z} 1`

      { openssl smime -decrypt -inkey ./client2-key.pem \
            -in ./t${1}.ENCRYPT |
         openssl smime -verify -CAfile ./ca.pem; } >>${ERR} 2>&1
      check_ex0 ${_z} # XXX pipe..
      _z=`add ${_z} 1`

      ${rm} ./t${1}.ENCRYPT
      echo bla | ${MAILX} ${ARGS} \
         -Smta=test://t${1}.ENCRYPT \
         -Ssmime-force-encryption \
         -Ssmime-encrypt-recei@ver.com=./client2-cert.pem \
         -Sfrom=test@localhost \
         -s 'S/MIME test' recei@ver.com >>${ERR} 2>&1
      check_ex0 ${_z}-estat
      ${sed} -e '/^$/,$d' < ./t${1}.ENCRYPT > ./t${_z}
      check ${_z} - ./t${_z} '1324731554 359'
      _z=`add ${_z} 1`

      # Note: deduce from *sign-cert*, not from *from*!
      printf 'decrypt ./t%s\nx\n' "${_z}" |
         ${MAILX} ${ARGS} \
         -Ssmime-sign-cert-recei@ver.com=./client2-pair.pem \
         -Serrexit -R -f ./t${1}.ENCRYPT >>${ERR} 2>&1
      check ${_z} 0 ./t${_z} '3114464078 454'
      _z=`add ${_z} 1`

      openssl smime ${_ossl} -decrypt -inkey ./client2-key.pem \
         -in ./t${1}.ENCRYPT >>${ERR} 2>&1
      check_ex0 ${_z}

      unset _z _pass _osslreq _ossl
   }
   #}}}

   doit 0
   doit 10

   t_epilog "${@}"
} # }}}
# }}}

# OPT_NET_TEST {{{
t_net_pop3() { # {{{ TODO TLS tests, then also EXTERN*
   t_prolog "${@}"

   if [ -n "${TESTS_NET_TEST}" ] && have_feat pop3; then :; else
      t_echoskip '[!NET_TEST or !POP3]'
      t_epilog "${@}"
      return
   fi

   pop3_logged_in() { # {{{
      printf '\002
+OK Logged in.
\001
STAT
\002
+OK 2 506
\001
LIST 1
\002
+OK 1 258
\001
LIST 2
\002
+OK 2 248
\001
TOP 1 0
\002
+OK
Return-Path: <steffen@kdc.localdomain>
Delivered-To: root@localhost
Date: Fri, 16 Aug 2019 19:46:20 +0200
From: steffen@kdc.localdomain
To: root@localhost
Subject: The GSSAPI dance is done!
Message-ID: <20190816174620.LeViGqO2@kdc.localdomain>

.
\001
TOP 2 0
\002
+OK
Return-Path: <steffen@kdc.localdomain>
Delivered-To: root@localhost
Date: Sat, 17 Aug 2019 23:21:25 +0200
From: steffen@kdc.localdomain
To: root@localhost
Subject: Hi from FreeBSD
Message-ID: <20190817212125.28sI5X7c@kdc.localdomain>

.
\001
QUIT
\002
+OK Logging out.
'
   } # }}}

   # Authentication types {{{
   t__net_script .t.sh pop3 \
      -Spop3-auth=plain -Spop3-no-apop -Snopop3-use-starttls
   { printf '\002
+OK Dovecot ready. <314.1.5d6ad59f.Rq8miBAdE0uUT/0GGKg2bA==@arch-2019>
\001
USER steffen
\002
+OK
\001
PASS Sway
' &&
      pop3_logged_in; } | ../net-test .t.sh > "${MBOX}" 2>&1
   check 1 0 "${MBOX}" '3754674759 160'

   if have_feat md5; then
      t__net_script .t.sh pop3 \
         -Spop3-auth=plain -Snopop3-use-starttls
      { printf '\002
+OK Dovecot ready. <314.1.5d6ad59f.Rq8miBAdE0uUT/0GGKg2bA==@arch-2019>
\001
APOP steffen 4f66ea9bf092117b009b9f8d928c656d
' &&
         pop3_logged_in; } | ../net-test .t.sh > "${MBOX}" 2>&1
      check 2 0 "${MBOX}" '3754674759 160'
   else
      t_echoskip '2:[!MD5]'
   fi

   if false && have_feat tls; then # TODO TLS-NET-SERV
      t__net_script .t.sh pop3 \
         -Spop3-auth=xoauth2 -Snopop3-use-starttls
      { printf '\001
+OK Dovecot ready. <314.1.5d6ad59f.Rq8miBAdE0uUT/0GGKg2bA==@arch-2019>
\002
AUTH XOAUTH2 dXNlcj1zdGVmZmVuAWF1dGg9QmVhcmVyIFN3YXkBAQ==
' &&
         pop3_logged_in; } | ../net-test .t.sh > "${MBOX}" 2>&1
      check 3 0 "${MBOX}" '3754674759 160'
   else
      t_echoskip '3:[false/TODO/!TLS]'
   fi
   # }}}

   t_epilog "${@}"
} # }}}

t_net_imap() { # {{{ TODO TLS tests, then also EXTERN*
   t_prolog "${@}"

   if [ -n "${TESTS_NET_TEST}" ] && have_feat imap; then :; else
      t_echoskip '[!NET_TEST or !IMAP]'
      t_epilog "${@}"
      return
   fi

   imap_hello() { # {{{
      printf '\002
* OK [CAPABILITY IMAP4rev1 SASL-IR LOGIN-REFERRALS ID ENABLE IDLE LITERAL+ STARTTLS AUTH=PLAIN AUTH=LOGIN AUTH=CRAM-MD5 AUTH=GSSAPI AUTH=XOAUTH2 AUTH=EXTERNAL] Dovecot ready.
\001
T1 CAPABILITY
\002
* CAPABILITY IMAP4rev1 SASL-IR LOGIN-REFERRALS ID ENABLE IDLE LITERAL+ STARTTLS AUTH=PLAIN AUTH=LOGIN AUTH=CRAM-MD5 AUTH=GSSAPI AUTH=XOAUTH2 AUTH=EXTERNAL
T1 OK Pre-login capabilities listed, post-login capabilities have more.
'
   } # }}}

   imap_logged_in() { # {{{
      __xno1__=2
      [ ${#} -eq 1 ] && __xno1__=${1}

      __xno2__=`add ${__xno1__} 1`
      __xno3__=`add ${__xno2__} 1`
      __xno4__=`add ${__xno3__} 1`
      __xno5__=`add ${__xno4__} 1`
      __xno6__=`add ${__xno5__} 1`

      printf '\002
T%s OK [CAPABILITY IMAP4rev1 SASL-IR LOGIN-REFERRALS ID ENABLE IDLE SORT SORT=DISPLAY THREAD=REFERENCES THREAD=REFS THREAD=ORDEREDSUBJECT MULTIAPPEND URL-PARTIAL CATENATE UNSELECT CHILDREN NAME SPACE UIDPLUS LIST-EXTENDED I18NLEVEL=1 CONDSTORE QRESYNC ESEARCH ESORT SEARCHRES WITHIN CONTEXT=SEARCH LIST-STATUS BINARY MOVE SNIPPET=FUZZY PREVIEW=FUZZY LITERAL+ NOTIFY SPECIAL-USE] Logged in
\001
T%s EXAMINE "INBOX"
\002
* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)
* OK [PERMANENTFLAGS ()] Read-only mailbox.
* 2 EXISTS
* 0 RECENT
* OK [UNSEEN 2] First unseen.
* OK [UIDVALIDITY 1565715806] UIDs valid
* OK [UIDNEXT 38] Predicted next UID
T%s OK [READ-ONLY] Examine completed (0.001 + 0.000 secs).
\001
T%s FETCH 1:2 (FLAGS UID)
\002
* 1 FETCH (FLAGS (\\Seen) UID 36)
* 2 FETCH (FLAGS () UID 37)
T%s OK Fetch completed (0.001 + 0.000 secs).
\001
T%s FETCH 1:2 (RFC822.SIZE INTERNALDATE)
\002
* 1 FETCH (RFC822.SIZE 258 INTERNALDATE "16-Aug-2019 19:46:20 +0200")
* 2 FETCH (RFC822.SIZE 248 INTERNALDATE "17-Aug-2019 23:21:27 +0200")
T%s OK Fetch completed (0.001 + 0.000 secs).
\001
T%s UID FETCH 36:37 (RFC822.HEADER)
\002
* 1 FETCH (UID 36 RFC822.HEADER {253}
Return-Path: <steffen@kdc.localdomain>
Delivered-To: root@localhost
Date: Fri, 16 Aug 2019 19:46:20 +0200
From: steffen@kdc.localdomain
To: root@localhost
Subject: The GSSAPI dance is done!
Message-ID: <20190816174620.LeViGqO2@kdc.localdomain>

)
* 2 FETCH (UID 37 RFC822.HEADER {243}
Return-Path: <steffen@kdc.localdomain>
Delivered-To: root@localhost
Date: Sat, 17 Aug 2019 23:21:25 +0200
From: steffen@kdc.localdomain
To: root@localhost
Subject: Hi from FreeBSD
Message-ID: <20190817212125.28sI5X7c@kdc.localdomain>

)
T%s OK Fetch completed (0.001 + 0.000 secs).
\001
T%s LOGOUT
\002
* BYE Logging out
' \
      "${__xno1__}" \
      "${__xno2__}" "${__xno2__}" \
      "${__xno3__}" "${__xno3__}" \
      "${__xno4__}" "${__xno4__}" \
      "${__xno5__}" "${__xno5__}" \
      "${__xno6__}"
   } # }}}

   t__net_script .t.sh imap \
      -Simap-auth=login -Snoimap-use-starttls
   { imap_hello && printf '\001
T2 LOGIN "steffen" "Sway"
' &&
      imap_logged_in; } | ../net-test .t.sh > "${MBOX}" 2>&1
   check 1 0 "${MBOX}" '4233548649 160'

   if false && have_feat tls; then # TODO TLS-NET-SERV
      t__net_script .t.sh imap \
         -Simap-auth=oauthbearer -Snoimap-use-starttls
      { imap_hello && printf '\002
T2 AUTHENTICATE XOAUTH2 dXNlcj1zdGVmZmVuAWF1dGg9QmVhcmVyIFN3YXkBAQ==
' &&
         imap_logged_in; } | ../net-test .t.sh > "${MBOX}" 2>&1
      check 2 0 "${MBOX}" '4233548649 160'
   else
      t_echoskip '2:[false/TODO/!TLS]'
   fi

   if have_feat md5; then
      t__net_script .t.sh imap \
         -Simap-auth=cram-md5 -Snoimap-use-starttls
      { imap_hello && printf '\001
T2 AUTHENTICATE CRAM-MD5
\002
+ PDAzMjYxNTc5NDU2Mzc3MTAuMTU2NzI5NDU0MUBhcmNoLTIwMTk+
\001
c3RlZmZlbiA1MTdlZDhlNDhkMDhhN2FkNDUwZDdlNzljYWFhMzNmZQ==
' &&
         imap_logged_in; } | ../net-test .t.sh > "${MBOX}" 2>&1
      check 2 0 "${MBOX}" '4233548649 160'
   else
      t_echoskip '2:[!MD5]'
   fi

   t_epilog "${@}"
} # }}}

t_net_smtp() { # {{{ TODO v15: drop smtp-hostname tests
   t_prolog "${@}"

   if [ -n "${TESTS_NET_TEST}" ] && have_feat smtp; then :; else
      t_echoskip '[!NET_TEST or !SMTP]'
      t_epilog "${@}"
      return
   fi

   t__tls_certs

   helo= mail_from= from= msgid= head_tail=

   have_feat tls && ext_tls=250-STARTTLS || ext_tls=

   # SMTP net-test script {{{
   smtp__script() {
      file=${1}
      proto=${2}
      shift 2

      ${cat} <<-_EOT > ./t.sh
		#!${SHELL} -
		<"${file}" LC_ALL=C ${MAILX} ${ARGS} -Sstealthmua=noagent \\
			-S tls-ca-no-defaults -S tls-ca-file=./ca.pem \\
			-Suser=steffen -Spassword=Sway -s ub \\
			-S 'mta=${proto}://localhost:'\${1} \\
			${@} \\
			ex@am.ple
		_EOT
      ${chmod} 0755 ./t.sh
   }

   smtp_script_file() {
      file=${1}
      shift
      helo=reproducible_build
      mail_from=reproducible_build@${helo}
      from=${mail_from}
      msgid='
Message-ID: <19961002015007.AQACA%reproducible_build@reproducible_build>'
      smtp__script ${file} "$@"
   }

   smtp_script() {
      smtp_script_file /dev/null "$@"
   }

   smtp_script_hostname() {
      helo=am.ple
      mail_from=reproducible_build@${helo}
      from=${mail_from}
      msgid='
Message-ID: <19961002015007.AQACAAAA@am.ple>'
      smtp__script /dev/null "$@" -Shostname=am.ple
   }

   smtp_script_hostname_smtp_hostname() {
      helo=am.ple
      mail_from=steffen@am2.ple2
      from=reproducible_build@${helo}
      msgid='
Message-ID: <19961002015007.AQACA%steffen@am2.ple2>'
      smtp__script /dev/null "$@" -Shostname=am.ple -Ssmtp-hostname=am2.ple2
   }

   smtp_script_hostname_smtp_hostname_empty() {
      helo=am.ple
      mail_from=steffen@am.ple
      from=reproducible_build@${helo}
      msgid='
Message-ID: <19961002015007.AQACA%steffen@am.ple>'
      smtp__script /dev/null "$@" -Shostname=am.ple -Ssmtp-hostname=
   }

   smtp_script_from() {
      helo=reproducible_build
      mail_from=steffen.ex@am.ple
      from=${mail_from}
      msgid='
Message-ID: <19961002015007.AQACA%steffen.ex@am.ple>'
      smtp__script /dev/null "$@" -Sfrom=${from}
   }

   smtp_script_from_hostname() {
      helo=am2.ple2
      mail_from=steffen.ex@am.ple
      from=${mail_from}
      msgid='
Message-ID: <19961002015007.AQACAAAA@am2.ple2>'
      smtp__script /dev/null "$@" \
         -Sfrom=${mail_from} -Shostname=am2.ple2
   }

   smtp_script_from_hostname_smtp_hostname() {
      helo=am2.ple2
      mail_from=steffen@am3.ple3
      from=steffen.ex@am.ple
      msgid='
Message-ID: <19961002015007.AQACA%steffen@am3.ple3>'
      smtp__script /dev/null "$@" -Sfrom=${from} \
         -Shostname=am2.ple2 -Ssmtp-hostname=am3.ple3
   }

   smtp_script_from_hostname_smtp_hostname_empty() {
      helo=am2.ple2
      mail_from=steffen@am2.ple2
      from=steffen.ex@am.ple
      msgid='
Message-ID: <19961002015007.AQACA%steffen@am2.ple2>'
      smtp__script /dev/null "$@" -Sfrom=${from} \
         -Shostname=am2.ple2 -Ssmtp-hostname=
   }

   smtp_script_from_hostname_smtp_from() {
      helo=am.ple
      mail_from=steffen2@am2.ple2
      from=steffen.ex@am.ple
      msgid='
Message-ID: <19961002015007.AQACAAAA@am.ple>'
      smtp__script /dev/null "$@" -Shostname=am.ple \
         -Sfrom=${from} -Ssmtp-from=steffen2@am2.ple2
   }
   # }}}

   # HE-EH-LOs {{{
   smtp_helo() {
      printf '\002
220 arch-2019 ESMTP Postfix
\001
HELO %s
\002
250 arch-2019, hi dude
' \
      "${helo}"
   }

   smtp_ehlo() {
      [ ${#} -eq 0 ] && printf '\002\n220 arch-2019 ESMTP Postfix\n'
      printf '\001
EHLO %s
\002
250-arch-2019, hi dude
250-AUTH PLAIN LOGIN CRAM-MD5 XOAUTH2 OAUTHBEARER EXTERNAL
250-ENHANCEDSTATUSCODES
' \
      "${helo}"
      [ ${#} -eq 0 ] && [ -n "${ext_tls}" ] && printf '%s\n' "${ext_tls}"
      printf '250-8BITMIME\n250 PIPELINING\n'
   }
   # }}}

   smtp_auth_ok() { printf '\002\n235 2.7.0 Authentication successful\n'; }

   # After AUTH {{{
   smtp_mail_from_to() {
      printf '\001\nMAIL FROM:<%s>\n\002\n250 2.1.0 Ok\n' "${mail_from}"
      printf '\001\nRCPT TO:<%s>\n\002\n250 2.1.5 Ok\n' "${@}"
      printf '\001\nDATA\n'
   }

   mail_from_8bitmime=
   smtp_mail_from_to_pipelining() {
      __mftp__=
      [ -n "${mail_from_8bitmime}" ] && __mftp__=' BODY='${mail_from_8bitmime}
      printf '\001\nMAIL FROM:<%s>%s\n' "${mail_from}" "${__mftp__}"
      printf 'RCPT TO:<%s>\n' "${@}"
      printf 'DATA\n'
   }

   smtp_data() {
      printf '\002\n'
      if [ ${#} -gt 0 ]; then
         printf '250 2.1.0 Ok\n'
         while [ ${#} -gt 0 ]; do
            printf '250 2.1.0 Ok\n'
            shift
         done
      fi
      printf '354 End data with <CR><LF>.<CR><LF>\n\001\n'
      smtp_date_from
   }

   smtp_date_from() {
      printf 'Date: Wed, 02 Oct 1996 01:50:07 +0000\nAuthor: %s\nFrom: %s\n' \
         "${from}" "${from}"
   }

   smtp_to() { printf 'To: ex@am.ple\n'; }

   smtp_head_tail() { printf 'Subject: ub%s%s\n\n' "${msgid}" "${head_tail}"; }

   smtp_head_all() {
      smtp_mail_from_to ex@am.ple &&
      smtp_data && smtp_to && smtp_head_tail
   }

   smtp_quit() {
      printf '.
\002
250 2.0.0 Ok: queued as 78FFC20305
\001
QUIT
\002
221 2.0.0 Bye
'
   }

   smtp_quit_pipelining() {
      printf '.
QUIT
\002
250 2.0.0 Ok: queued as 78FFC20305
221 2.0.0 Bye
'
   }

   smtp_go() { smtp_head_all && smtp_quit; }
   # }}}

   # Check the *from* / *hostname* / *smtp-from* .. interaction {{{
   smtp_script smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t1 2>&1
   check 1 0 ./t1 '4294967295 0'

   smtp_script_hostname smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t2 2>&1
   check 2 0 ./t2 '4294967295 0'

   smtp_script_hostname_smtp_hostname smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t3 2>&1
   check 3 0 ./t3 '4294967295 0'

   smtp_script_hostname_smtp_hostname_empty smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t4 2>&1
   check 4 0 ./t4 '4294967295 0'

   smtp_script_from smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t5 2>&1
   check 5 0 ./t5 '4294967295 0'

   smtp_script_from_hostname smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t6 2>&1
   check 6 0 ./t6 '4294967295 0'

   smtp_script_from_hostname_smtp_hostname smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t7 2>&1
   check 7 0 ./t7 '4294967295 0'

   smtp_script_from_hostname_smtp_hostname_empty smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t8 2>&1
   check 8 0 ./t8 '4294967295 0'

   smtp_script_from_hostname_smtp_from smtp -Ssmtp-config=-ehlo
   { smtp_helo && smtp_go; } | ../net-test t.sh > ./t9 2>&1
   check 9 0 ./t9 '4294967295 0'
   # }}}

   # Real EHLO authentication types {{{
   smtp_script smtp -Ssmtp-config=-all,,plain,, #,ehlo<-implied,plain
   { smtp_ehlo && printf '\001
AUTH PLAIN AHN0ZWZmZW4AU3dheQ==
' &&
      smtp_auth_ok && smtp_go; } | ../net-test t.sh > ./tauth-1 2>&1
   check auth-1 0 ./tauth-1 '4294967295 0'

   smtp_script smtp -Ssmtp-config=-all,ehlo,login
   { smtp_ehlo && printf '\001
AUTH LOGIN
\002
334 VXNlcm5hbWU6
\001
c3RlZmZlbg==
\002
334 UGFzc3dvcmQ6
\001
U3dheQ==
' &&
      smtp_auth_ok && smtp_go; } | ../net-test t.sh > ./tauth-2 2>&1
   check auth-2 0 ./tauth-2 '4294967295 0'

   if have_feat tls; then
      smtp_script smtps -Ssmtp-config=-all,ehlo,xoauth2
      { smtp_ehlo && printf '\001
AUTH XOAUTH2 dXNlcj1zdGVmZmVuAWF1dGg9QmVhcmVyIFN3YXkBAQ==
' &&
         smtp_auth_ok && smtp_go; } | ../net-test -S t.sh > ./tauth-3 2>&1
      check auth-3 0 ./tauth-3 '4294967295 0'
   else
      t_echoskip 'auth-3:[!TLS]'
   fi

   if have_feat md5; then
      smtp_script smtp -Ssmtp-config=-all,ehlo,,cram-md5
      { smtp_ehlo && printf '\001
AUTH CRAM-MD5
\002
334 PDM2MzI5MzIyMDE2MDM5NDUuMTU2NzQ1NTkxOUBhcmNoLTIwMTk+
\001
c3RlZmZlbiAwZjJmNmViMzI2YmE5M2UxM2YyM2M5MjhjZDYzMTQxOQ==
' &&
         smtp_auth_ok && smtp_go; } | ../net-test t.sh > ./tauth-4 2>&1
      check auth-4 0 ./tauth-4 '4294967295 0'
   else
      t_echoskip 'auth-4:[!MD5]'
   fi

   # STARTTLS, and more TLS AUTH things
   if have_feat tls; then
      smtp_script smtp -Ssmtp-config=-all,xoauth2
      { smtp_ehlo && printf '\001\nNOT REACHED\n'; } |
            ../net-test -s t.sh > ./tauth-5 2>>${ERR}
      check_exn0 auth-5 8

      smtp_script smtp -Ssmtp-config=-all,starttls,xoauth2
      { smtp_ehlo && printf '\001
STARTTLS
\003
220 2.0.0 Ready to start TLS
' &&
         smtp_ehlo 0 && printf '\001
AUTH XOAUTH2 dXNlcj1zdGVmZmVuAWF1dGg9QmVhcmVyIFN3YXkBAQ==
' &&
         smtp_auth_ok && smtp_go; } | ../net-test -s t.sh > ./tauth-6 2>&1
      check auth-6 0 ./tauth-6 '4294967295 0'

      smtp_script smtp -Ssmtp-config=-all,starttls,externanon \
         -Stls-config-pairs=Certificate=client-pair.pem
      { smtp_ehlo && printf '\001
STARTTLS
\003
220 2.0.0 Ready to start TLS
' &&
         smtp_ehlo 0 && printf '\001
AUTH EXTERNAL =
' &&
         smtp_auth_ok && smtp_go; } | ../net-test -U -s t.sh > ./tauth-7 2>&1
      check auth-7 0 ./tauth-7 '4294967295 0'

      smtp_script smtps -Ssmtp-config=-all,external \
         -Stls-config-pairs=Certificate=client-pair.pem
      { smtp_ehlo && printf '\001
AUTH EXTERNAL c3RlZmZlbg==
' &&
         smtp_auth_ok && smtp_go; } | ../net-test -U -S t.sh > ./tauth-8 2>&1
      check auth-8 0 ./tauth-8 '4294967295 0'

      smtp_script smtps -Ssmtp-config=-all,oauthbearer
      { smtp_ehlo && printf '\001
AUTH OAUTHBEARER bixhPXN0ZWZmZW4sAWhvc3Q9bG9jYWxob3N0AXBvcnQ9NTAwMDABYXV0aD1CZWFyZXIgU3dheQEB
' &&
         smtp_auth_ok && smtp_go; } | ../net-test -S t.sh > ./tauth-9 2>&1
      check auth-9 0 ./tauth-9 '4294967295 0'
   else
      # Why not that instead?
      smtp_script smtp -Ssmtp-config=-all,xoauth2
      { smtp_ehlo && printf '\001\nNOT REACHED\n'; } |
            ../net-test -s t.sh > ./tauth-5 2>>${ERR}
      check_exn0 auth-5 8

      t_echoskip 'auth-{6-9}:[!TLS]'
   fi
   # }}}

   # Some data feeding {{{
   # body data
   ${awk} '
      BEGIN{
         for(lnlen = i = 0; i < 9999; ++i){
            j = "[" i "]"
            printf j
            if((lnlen += length(j)) >= 70){
               printf "\n"
               lnlen = 0
            }
         }
         if(lnlen > 0)
            printf "\n"
      }' > ./t.dat

   smtp_script_file ./t.dat smtp -Ssmtp-config=-all &&
   { smtp_helo && smtp_head_all && ${cat} ./t.dat && smtp_quit; } |
      ../net-test t.sh > ./tdata-1 2>&1
   check data-1 0 ./tdata-1 '4294967295 0'

   # more RCPT TO:<>
   rcpt_to=`${awk} '
         BEGIN{
            for(i = 0; i < 100; i += 2)
               printf "ex-" i "@am.ple "
            printf "ex@am.ple "
            for(i = 1; i < 100; i += 2)
               printf "ex-" i "@am.ple "
         }'`
   tolist=`${awk} '
         BEGIN{
            for(i = 0; i < 100; i += 2)
               printf "ex-%s@am.ple ", i
         }'`
   cclist=`${awk} '
         BEGIN{
            for(i = 1; i < 100; i += 2)
               printf "-c ex-%s@am.ple ", i
         }'`

   smtp_rcpt_to() {
      __srt_file__=${7} __srt_xfile__=:
      if [ -z "${__srt_file__}" ]; then
         __srt_file__=/dev/null
      else
         __srt_xfile__="${cat} \"${__srt_file__}\""
      fi
      smtp_script_file ${__srt_file__} smtp -Ssmtp-config=${1} ${8} \
         ${cclist} ${tolist} &&
      { ${2} && ${3} $rcpt_to &&
            eval "smtp_data ${4}" && ${awk} '
               function doit(i, j){
                  printf j
                  lnlen = length(j)
                  for(; i <= 100; i += 2){
                     if(i + 2 > 100){
                        printf "ex%s@am.ple\n", (i == 100 ? "" : "-99")
                        break
                     }

                     j = "ex-" i "@am.ple,"
                     if((lnlen += length(j)) >= 60){
                        lnlen = 1;
                        j = j "\n "
                     }else
                        j = j " "
                     printf j
                  }
               }
               BEGIN{
                  doit(0, "To: ")
                  doit(1, "Cc: ")
               }' &&
            smtp_head_tail && eval ${__srt_xfile__} && ${5}; } |
         ../net-test t.sh > ./${6} 2>>${ERR}
   }

   smtp_rcpt_to -ehlo \
      smtp_helo \
      smtp_mail_from_to '' \
      smtp_quit \
      tdata-2
   check data-2 0 ./tdata-2 '4294967295 0'

   mail_from_8bitmime=7BIT
   smtp_rcpt_to all,-starttls,-allmechs \
      smtp_ehlo \
      smtp_mail_from_to_pipelining "$rcpt_to" \
      smtp_quit_pipelining \
      tdata-3
   mail_from_8bitmime=
   check data-3 0 ./tdata-3 '4294967295 0'

   if have_feat iconv; then
      echo Pr�sterchen > ./tdata-4.in
      mail_from_8bitmime=8BITMIME
      head_tail='
MIME-Version: 1.0
Content-Type: text/plain; charset=latin1
Content-Transfer-Encoding: 8bit'
      smtp_rcpt_to all,-starttls,-allmechs \
         smtp_ehlo \
         smtp_mail_from_to_pipelining "$rcpt_to" \
         smtp_quit_pipelining \
         tdata-4 tdata-4.in \
         '-Smime-encoding=8bit -S ttycharset=LATIN1 -S charset-8bit=LATIN1'
      mail_from_8bitmime= head_tail=
      check data-4 0 ./tdata-4 '4294967295 0'
   else
      t_echoskip '4:[!ICONV]'
   fi
   # }}}

   t_epilog "${@}"
} # }}}
# }}}

# Test support {{{
# Message generation and other header/message content {{{
t__gen_msg() {
   t___gen_msg '' "${@}"
}

t__gen_mimemsg() {
   t___gen_msg 1 "${@}"
}

t___gen_msg() {
   ismime=${1}
   shift

   t___header() {
      printf '%s: ' ${1}
      case "${3}" in
      [0-9]*)
         ___hi=1
         while [ ${___hi} -le ${3} ]; do
            [ ${___hi} -gt 1 ] && printf ', '
            printf '%s%s <%s%s@exam.ple>' ${1} ${___hi} ${2} ${___hi}
            ___hi=`add ${___hi} 1`
         done
         ;;
      *)
         printf '%s' "${3}"
         ;;
      esac
      printf '\n'
   }

   printf 'From reproducible_build Wed Oct  2 01:50:07 1996
Date: Wed, 02 Oct 1996 01:50:07 +0000
'

   body=Body
   while [ ${#} -ge 2 ]; do
      case "${1}" in
      from) t___header From from "${2}";;
      to) t___header To to "${2}";;
      cc) t___header Cc cc "${2}";;
      bcc) t___header Bcc bcc "${2}";;
      subject) printf 'Subject: %s\n' "${2}";;
      body) body="${2}";;
      esac
      shift 2
   done

   if [ -z "${ismime}" ]; then
      printf '\n%s\n\n' "${body}"
   else
      printf 'MIME-Version: 1.0
Message-ID: <20200204225307.FaKeD%%bo@oo>
Content-Type: multipart/mixed; boundary="=BOUNDOUT="

--=BOUNDOUT=
Content-Type: multipart/alternative; boundary==BOUNDIN=

--=BOUNDIN=
Content-Type: text/plain; charset=utf-8
Content-Transfer-Encoding: 8-bit

%s

--=BOUNDIN=
Content-Type: text/html; charset=utf-8
Content-Transfer-Encoding: 8-bit

<HTML><BODY>%s<BR></BODY></HTML>

--=BOUNDIN=--

--=BOUNDOUT=
Content-Type: text/troff

Golden Brown

--=BOUNDOUT=
Content-Type: text/x-uuencode

Aprendimos a quererte
--=BOUNDOUT=--

' "${body}" "${body}"
   fi
}

t__x1_msg() {
   ${cat} <<-_EOT
	From neverneverland  Sun Jul 23 13:46:25 2017
	Subject: Bugstop: five miles out 1
	Reply-To: mister originator1 <mr1@originator>
	From: mister originator1 <mr1@originator>
	To: bugstop-commit@five.miles.out
	Cc: is1@a.list
	In-reply-to: <20170719111113.bkcMz%laber1@backe.eu>
	Date: Wed, 19 Jul 2017 09:22:57 -0400
	Message-Id: <20170719132257.766AF781267-1@originator>
	Mail-Followup-To: bugstop@five.miles.out, laber@backe.eu, is@a.list
	Status: RO
	
	That's appalling, I.
	
	_EOT
}

t__x2_msg() {
   ${cat} <<-_EOT
	From neverneverland  Sun Jul 23 13:47:25 2017
	Subject: Bugstop: five miles out 2
	Reply-To: mister originator2 <mr2@originator>
	From: mister originator2 <mr2@originator>
	To: bugstop-commit@five.miles.out
	Cc: is2@a.list
	In-reply-to: <20170719111113.bkcMz%laber2@backe.eu>
	Date: Wed, 19 Jul 2017 09:23:57 -0400
	Message-Id: <20170719132257.766AF781267-2@originator>
	Status: RO
	
	That's appalling, II.
	
	_EOT
}

t__x3_msg() {
   ${cat} <<-_EOT
	From neverneverland  Sun Jul 23 13:48:25 2017
	Subject: Bugstop: five miles out 3
	Reply-To: mister originator3 <mr3@originator>
	From: mister originator3 <mr3@originator>
	To: bugstop-commit@five.miles.out
	Cc: is3@a.list
	In-reply-to: <20170719111113.bkcMz%laber3@backe.eu>
	Date: Wed, 19 Jul 2017 09:24:57 -0400
	Message-Id: <20170719132257.766AF781267-3@originator>
	Status: RO
	
	That's appalling, III.
	
	_EOT
}

t__put_subject() {
   # MIME encoding (QP) stress message subject
   printf 'Äbrä  Kä?dä=brö 	 Fü?di=bus? '\
'adadaddsssssssddddddddddddddddddddd'\
'ddddddddddddddddddddddddddddddddddd'\
'ddddddddddddddddddddddddddddddddddd'\
'dddddddddddddddddddd Hallelulja? Od'\
'er?? eeeeeeeeeeeeeeeeeeeeeeeeeeeeee'\
'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee'\
'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee f'\
'fffffffffffffffffffffffffffffffffff'\
'fffffffffffffffffffff ggggggggggggg'\
'ggggggggggggggggggggggggggggggggggg'\
'ggggggggggggggggggggggggggggggggggg'\
'ggggggggggggggggggggggggggggggggggg'\
'gggggggggggggggg'
}

t__put_body() {
   # MIME encoding (QP) stress message body
   printf \
'Ich bin eine DÖS-Datäi mit sehr langen Zeilen und auch '\
'sonst bin ich ganz schön am Schleudern, da kannste denke '\
"wasde willst, gelle, gelle, gelle, gelle, gelle.\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst \r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 1\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 12\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 123\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 1234\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 12345\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 123456\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 1234567\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 12345678\r\n"\
"Ich bin eine DÖS-Datäi mit langen Zeilen und auch sonst 123456789\r\n"\
"Unn ausserdem habe ich trailing SP/HT/SP/HT whitespace 	 	\r\n"\
"Unn ausserdem habe ich trailing HT/SP/HT/SP whitespace	 	 \r\n"\
"auf den zeilen vorher.\r\n"\
"From am Zeilenbeginn und From der Mitte gibt es auch.\r\n"\
".\r\n"\
"Die letzte Zeile war nur ein Punkt.\r\n"\
"..\r\n"\
"Das waren deren zwei.\r\n"\
" \r\n"\
"Die letzte Zeile war ein Leerschritt.\n"\
"=VIER = EQUAL SIGNS=ON A LINE=\r\n"\
"Prösterchen.\r\n"\
".\n"\
"Die letzte Zeile war nur ein Punkt, mit Unix Zeilenende.\n"\
"..\n"\
"Das waren deren zwei.  ditto.\n"\
"Prösterchen.\n"\
"Unn ausseerdem habe ich trailing SP/HT/SP/HT whitespace 	 	\n"\
"Unn ausseerdem habe ich trailing HT/SP/HT/SP whitespace	 	 \n"\
"auf den zeilen vorher.\n"\
"ditto.\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.1"\
"\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.12"\
"\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.12"\
"3\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.12"\
"34\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.12"\
"345\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.12"\
"3456\n"\
"QP am Zeilenende über soft-nl hinweg\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende."\
"ö123\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende."\
"1ö23\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende."\
"12ö3\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende."\
"123ö\n"\
"=VIER = EQUAL SIGNS=ON A LINE=\n"\
" \n"\
"Die letzte Zeile war ein Leerschritt.\n"\
' '
}
# }}}

t__net_script() {
   file=${1}
   proto=${2}
   shift 2

   t__tls_certs

   ${cat} <<-_EOT > ${file}
		#!${SHELL} -
		</dev/null ${MAILX} -# ${ARGS} \\
			-S tls-ca-no-defaults -S tls-ca-file=./ca.pem \\
			-Suser=steffen -Spassword=Sway \\
			${@} \\
			-Y 'File ${proto}://localhost:'\${1} \\
			-Y 'h;q'
		_EOT
   ${chmod} 0755 ${file}
}

# TLS keys and certificates {{{
t__tls_certs() {
   if have_feat tls; then :; else
      return 1
   fi

   __tls_certs_harderr=
   while :; do
      [ -d ../t.tls.db ] && {
         t__tls__copy
         return ${?}
      }
      [  -n "${__tls_cert_harderr}" ] && return 1
      if ${mkdir} ../t.tls.db 2>/dev/null; then
         t__tls__create 2>>${ERR} 1>&2 || {
            e=${?}
            ${rm} -rf ../t.tls.db
            return ${e}
         }
      else
         __tls_certs_harderr=1
      fi
   done
}

t__tls__create() ( #{{{
   cd ../t.tls.db

   ${cat} <<-_EOT > ./t-root-ca.cnf
		extensions = ext_v3_ca
		[req]
		x509_extensions = ext_v3_ca
		distinguished_name = req_distinguished_name
		attributes = req_attributes
		prompt = no
		[req_distinguished_name]
		C = IT
		ST = ROOT-CA-ST
		L = ROOT-CA-L
		O = ROOT-CA-O
		OU = ROOT-CA-OU
		CN = ROOT-CA-CN
		emailAddress = test@root-ca.example
		# At one time AlpineLinux OpenSSL required that:
		[req_attributes]
		challengePassword = hi ca it is me me me
		# Extensions for a typical CA
		[ext_v3_ca]
		# PKIX recommendation
		subjectKeyIdentifier = hash
		authorityKeyIdentifier = keyid:always,issuer
		basicConstraints = critical,CA:true
	_EOT

   ${cat} <<-_EOT > ./t-root2-ca.cnf
		extensions = ext_v3_ca
		[req]
		x509_extensions = ext_v3_ca
		distinguished_name = req_distinguished_name
		attributes = req_attributes
		prompt = no
		[req_distinguished_name]
		C = IT
		ST = ROOT2-CA-ST
		L = ROOT2-CA-L
		O = ROOT2-CA-O
		OU = ROOT2-CA-OU
		CN = ROOT2-CA-CN
		emailAddress = test@root2-ca.example
		[req_attributes]
		challengePassword = hi ca it is me me me
		# Extensions for a typical CA
		[ext_v3_ca]
		# PKIX recommendation
		subjectKeyIdentifier = hash
		authorityKeyIdentifier = keyid:always,issuer
		basicConstraints = critical,CA:true
	_EOT

   ${cat} <<-_EOT > ./t-ca.cnf
		extensions = ext_v3_ca
		[req]
		x509_extensions = ext_v3_ca
		distinguished_name = req_distinguished_name
		attributes = req_attributes
		prompt = no
		[req_distinguished_name]
		C = IT
		ST = CA-ST
		L = CA-L
		O = CA-O
		OU = CA-OU
		CN = CA-CN
		emailAddress = test@ca.example
		[req_attributes]
		challengePassword = hi ca it is me me me
		# Extensions for a typical CA
		[ext_v3_ca]
		subjectKeyIdentifier = hash
		authorityKeyIdentifier = keyid:always,issuer
		basicConstraints = critical,CA:true
	_EOT

   ${cat} <<-_EOT > ./t-srv.cnf
		extensions = ext_srv_cert
		[req]
		x509_extensions = ext_srv_cert
		distinguished_name = req_distinguished_name
		attributes = req_attributes
		prompt = no
		[req_distinguished_name]
		C = IT
		ST = SERV-ST
		L = SERV-L
		O = SERV-O
		OU = SERV-OU
		CN = localhost
		emailAddress = test@srv.example
		[req_attributes]
		challengePassword = hi ca it is me me me
		[ext_srv_cert]
		basicConstraints = CA:FALSE
		extendedKeyUsage = critical,serverAuth,emailProtection
		subjectKeyIdentifier = hash
		authorityKeyIdentifier = keyid,issuer
	_EOT

   ${cat} <<-_EOT > ./t.cnf
		extensions = ext_usr_cert
		[req]
		x509_extensions = ext_usr_cert
		distinguished_name = req_distinguished_name
		attributes = req_attributes
		prompt = no
		[req_distinguished_name]
		C = IT
		ST = Over the
		L = rainbow
		O = S-mailx
		OU = S-mailx.tls
		CN = S-mailx.test3
		emailAddress = test@localhost
		[req_attributes]
		challengePassword = hi ca it is me me me
		[ext_usr_cert]
		basicConstraints = CA:FALSE
		extendedKeyUsage = critical,clientAuth,emailProtection
		subjectKeyIdentifier = hash
	_EOT

   ${cat} <<-_EOT > ./t2.cnf
		extensions = ext_usr_cert
		[req]
		x509_extensions = ext_usr_cert
		distinguished_name = req_distinguished_name
		attributes = req_attributes
		prompt = no
		[req_distinguished_name]
		C = IT
		ST = Over the
		L = rainbow
		O = S-mailx2
		OU = S-mailx2.tls
		CN = S-mailx2.test3
		emailAddress = test2@localhost
		[req_attributes]
		challengePassword = hi ca it is me me me
		[ext_usr_cert]
		basicConstraints = CA:FALSE
		extendedKeyUsage = critical,clientAuth,emailProtection
		subjectKeyIdentifier = hash
	_EOT

   ## Root CA
   openssl req -newkey rsa:1024 \
      -config t-root-ca.cnf -nodes \
      -keyout root-ca-key.pem -out root-ca-req.pem || exit 1
   openssl x509 -req -in root-ca-req.pem \
      -extfile t-root-ca.cnf \
      -signkey root-ca-key.pem \
      -out root-ca-cert.pem || exit 2

   ## Root CA 2
   openssl req -newkey rsa:1024 \
      -config t-root2-ca.cnf -nodes \
      -keyout root2-ca-key.pem -out root2-ca-req.pem || exit 1
   openssl x509 -req -in root2-ca-req.pem \
      -extfile t-root2-ca.cnf \
      -signkey root2-ca-key.pem \
      -out root2-ca-cert.pem || exit 2

   ${cat} root-ca-cert.pem root2-ca-cert.pem > ca.pem

   ## Server CA, sign with root CA
   openssl req -newkey rsa:1024 \
      -config t-ca.cnf -nodes \
      -keyout server-ca-key.pem -out server-ca-req.pem || exit 3
   openssl x509 -req -in server-ca-req.pem \
      -extfile t-ca.cnf \
      -CA root-ca-cert.pem -CAkey root-ca-key.pem -CAcreateserial \
      -out server-ca-cert.pem || exit 4

   ## Server certificate, sign with server CA
   openssl req -newkey rsa:1024 \
      -config t-srv.cnf -nodes \
      -keyout server-key.pem -out server-req.pem || exit 5
   openssl x509 -req -in server-req.pem \
      -extfile t-srv.cnf \
      -CA server-ca-cert.pem -CAkey server-ca-key.pem -CAcreateserial \
      -out server-cert.pem || exit 6
   ${cat} server-cert.pem \
      server-ca-cert.pem \
      > server-chain.pem

   # Same, password protected
   openssl req -newkey rsa:1024 \
      -config t.cnf \
      -passout pass:server-key-pass \
      -keyout server-pass-key.pem -out server-pass-req.pem || exit 7
   openssl x509 -req -in server-pass-req.pem \
      -extfile t.cnf \
      -CA server-ca-cert.pem -CAkey server-ca-key.pem -CAcreateserial \
      -out server-pass-cert.pem || exit 8
   ${cat} \
      server-pass-cert.pem \
      server-ca-cert.pem \
      > server-pass-chain.pem

   ##
   openssl dhparam -check -text -5 512 -out dh512.pem || exit 9

   ## Client certificate
   openssl req -newkey rsa:1024 \
      -config t.cnf -nodes \
      -keyout client-key.pem -out client-req.pem || exit 10
   openssl x509 -req -in client-req.pem \
      -extfile t.cnf \
      -CA root-ca-cert.pem -CAkey root-ca-key.pem -CAcreateserial \
      -out client-cert.pem || exit 11
   ${cat} \
      client-key.pem client-cert.pem \
      > client-pair.pem
   ${cat} \
      client-cert.pem \
      root-ca-cert.pem \
      > client-chain.pem

   # With password
   openssl req -newkey rsa:1024 \
      -config t.cnf \
      -passout pass:client-key-pass \
      -keyout client-pass-key.pem -out client-pass-req.pem || exit 12
   openssl x509 -req -in client-pass-req.pem \
      -extfile t.cnf \
      -passin pass:client-key-pass \
      -CA root-ca-cert.pem -CAkey root-ca-key.pem -CAcreateserial \
      -out client-pass-cert.pem || exit 13
   ${cat} \
      client-pass-key.pem client-pass-cert.pem \
      > client-pass-pair.pem
   ${cat} \
      client-pass-cert.pem \
      root-ca-cert.pem \
      > client-pass-chain.pem

   ## Client 2 certificate
   openssl req -newkey rsa:1024 \
      -config t2.cnf -nodes \
      -keyout client2-key.pem -out client2-req.pem || exit 10
   openssl x509 -req -in client2-req.pem \
      -extfile t2.cnf \
      -CA root2-ca-cert.pem -CAkey root2-ca-key.pem -CAcreateserial \
      -out client2-cert.pem || exit 11
   ${cat} \
      client2-key.pem client2-cert.pem \
      > client2-pair.pem
   ${cat} \
      client2-cert.pem \
      root2-ca-cert.pem \
      > client2-chain.pem

   printf '' > .t_tls_is_setup
) #}}}

t__tls__copy() {
   while :; do
      [ -f ../t.tls.db/.t_tls_is_setup ] && break
      if [ -z "${SUBSECOND_SLEEP}" ]; then
         sleep 1 &
      else
         sleep .25 &
      fi
      wait ${!}
   done
   ${cp} -f ../t.tls.db/*.* .
}
# }}}
# }}}

# Test all configs TODO does not cover all *combinations*, stupid!
cc_all_configs() { #{{{
   if [ ${JOBNO} -gt 1 ]; then
      JOBNO='-j '${JOBNO}
   else
      JOBNO=
   fi
   if [ -n "${NOCOLOUR}" ] || [ -n "${MAILX_CC_TEST_NO_COLOUR}" ]; then
      MAILX_CC_TEST_NO_COLOUR=1
      export MAILX_CC_TEST_NO_COLOUR
   fi

   < ${CONF} ${awk} '
      BEGIN{
         ALWAYS = "OPT_AUTOCC=1 OPT_AMALGAMATION=1"
         NOTME["OPT_AUTOCC_STACKPROT"] = 1
         NOTME["OPT_ALWAYS_UNICODE_LOCALE"] = 1
         NOTME["OPT_CROSS_BUILD"] = 1
         NOTME["OPT_AUTOCC"] = 1
         NOTME["OPT_AMALGAMATION"] = 1
         NOTME["OPT_DEBUG"] = 1
         NOTME["OPT_DEVEL"] = 1
         NOTME["OPT_ASAN_ADDRESS"] = 1
         NOTME["OPT_ASAN_MEMORY"] = 1
         NOTME["OPT_USAN"] = 1
         NOTME["OPT_EXTERNAL_MEM_CHECK"] = 1

         #OPTVALS
         OPTNO = 0

         MULCHOICE["OPT_ICONV"] = "VAL_ICONV"
            MULVALS["VAL_ICONV"] = 1
         MULCHOICE["OPT_IDNA"] = "VAL_IDNA"
            MULVALS["VAL_IDNA"] = 1

         #VALKEYS[0] = "VAL_RANDOM"
            VALVALS["VAL_RANDOM"] = 1
         VALNO = 0
      }
      /^[ 	]*OPT_/{
         sub(/^[ 	]*/, "")
         # This bails for UnixWare 7.1.4 awk(1), but preceeding = with \
         # does not seem to be a compliant escape for =
         #sub(/=.*$/, "")
         $1 = substr($1, 1, index($1, "=") - 1)
         if(!NOTME[$1])
            OPTVALS[OPTNO++] = $1
         next
      }
      /^[ 	]*VAL_/{
         sub(/^[ 	]*/, "")
         val = substr($0, index($0, "=") + 1)
         if(val ~ /^"/){
            val = substr(val, 2)
            val = substr(val, 1, length(val) - 1)
         }
         $1 = substr($1, 1, index($1, "=") - 1)
         if(MULVALS[$1])
            MULVALS[$1] = val
         else if(VALVALS[$1]){
            VALKEYS[VALNO++] = $1
            VALVALS[$1] = val
         }
         next
      }
      function onepass(addons){
         a_onepass__worker(addons, "1", "0")
         a_onepass__worker(addons, "0", "1")
      }
      function a_onepass__worker(addons, b0, b1){
         # Doing this completely sequentially and not doing make distclean in
         # between runs should effectively result in lesser compilations.
         # It is completely dumb nonetheless... TODO
         for(ono = 0; ono < OPTNO; ++ono){
            myconf = mula = ""
            for(i = 0; i < ono; ++i){
               myconf = myconf " " OPTVALS[i] "=" b0 " "
               if(b0 == "1"){
                  j = MULCHOICE[OPTVALS[i]]
                  if(j){
                     if(i + 1 == ono)
                        mula = j
                     else
                        myconf = myconf " " MULCHOICE[OPTVALS[i]] "=any "
                  }
               }
            }
            for(i = ono; i < OPTNO; ++i){
               myconf = myconf " " OPTVALS[i] "=" b1 " "
               if(b1 == "1"){
                  j = MULCHOICE[OPTVALS[i]]
                  if(j){
                     if(i + 1 == OPTNO)
                        mula = j;
                     else
                        myconf = myconf " " MULCHOICE[OPTVALS[i]] "=any "
                  }
               }
            }

            for(i in VALKEYS)
               myconf = VALKEYS[i] "=any " myconf

            myconf = myconf " " ALWAYS " " addons

            if(mula == "")
               print myconf
            else{
               i = split(MULVALS[mula], ia)
               j = "any"
               while(i >= 1){
                  j = ia[i--] " " j
                  print mula "=\"" j "\" " myconf
               }
            }
         }
      }
      END{
         # We cannot test NULL because of missing UI strings, which will end
         # up with different checksums
         print "CONFIG=NULLI OPT_AUTOCC=1"
            for(i in VALKEYS){
               j = split(VALVALS[VALKEYS[i]], ia)
               k = "any"
               while(j >= 1){
                  k = ia[j--] " " k
                  print VALKEYS[i] "=\"" k "\" CONFIG=NULLI OPT_AUTOCC=1"
               }
            }
         print "CONFIG=MINIMAL OPT_AUTOCC=1"
         print "CONFIG=NETSEND OPT_AUTOCC=1"
         print "CONFIG=MAXIMAL OPT_AUTOCC=1"
            for(i in VALKEYS){
               j = split(VALVALS[VALKEYS[i]], ia)
               k = "any"
               while(j >= 1){
                  k = ia[j--] " " k
                  print VALKEYS[i] "=\"" k "\" CONFIG=MAXIMAL OPT_AUTOCC=1"
               }
            }
         print "CONFIG=DEVEL OPT_AUTOCC=1"
         print "CONFIG=ODEVEL OPT_AUTOCC=1"

         print split_here

         onepass("OPT_DEBUG=1")
         onepass("")
      }
   ' | while read c; do
      if [ "$c" = split_here ]; then
         printf 'Predefined configs done, now OPT_ combinations\n'
         printf 'Predefined configs done, now OPT_ combinations\n' >&2
         ${SHELL} -c "cd ../ && ${MAKE} distclean"
         continue
      fi
      [ -f mk-config.h ] && ${cp} mk-config.h .ccac.h
      printf "\n\n##########\n${MAKE} ${JOBNO} config $c\n"
      printf "\n\n##########\n${MAKE} ${JOBNO} config $c\n" >&2
      ${SHELL} -c "cd .. && ${MAKE} ${JOBNO} config ${c}"
      if [ -f .ccac.h ] && ${cmp} mk-config.h .ccac.h; then
         printf 'Skipping after config, nothing changed\n'
         printf 'Skipping after config, nothing changed\n' >&2
         continue
      fi
      ${SHELL} -c "cd ../ && ${MAKE} ${JOBNO} build test"
   done
   ${rm} -f .ccac.h
   cd .. && ${MAKE} distclean
} #}}}

t_all() { #{{{
   # Absolute Basics
   jspawn eval
   jspawn call
   jspawn X_Y_opt_input_go_stack
   jspawn X_errexit
   jspawn Y_errexit
   jspawn S_freeze
   jspawn f_batch_order
   jspawn input_inject_semicolon_seq
   jspawn wysh
   jspawn commandalias # test now, save space later on!
   jspawn posix_abbrev
   jsync

   # Basics (variables, program logic, arg stuff etc.: all here)
   jspawn shcodec
   jspawn ifelse
   jspawn localopts
   jspawn local
   jspawn environ
   jspawn loptlocenv
   jspawn macro_param_shift
   jspawn csop # often used
   jspawn vexpr # often used
   jspawn call_ret
   jspawn xcall
   jspawn local_x_call_environ
   jspawn vpospar
   jspawn atxplode
   jspawn read
   jspawn readsh
   jspawn fop
   jspawn msg_number_list
   jsync

   # Send/RFC absolute basics
   jspawn addrcodec
   jspawn headerpick # (Just so we have a notion that it works a bit .. now)
   jspawn can_send_rfc
   jspawn mta_args
   jspawn reply
   jspawn forward
   jspawn resend
   jsync

   # VFS
   jspawn copy
   jspawn save
   jspawn move
   jspawn mbox
   jspawn maildir
   jspawn eml_and_stdin_pipe
   jsync

   # MIME and RFC basics
   jspawn mime_if_not_ascii
   jspawn mime_encoding
   jspawn xxxheads_rfc2047
   jspawn iconv_mbyte_base64
   jspawn iconv_mainbody
   jspawn mime_force_sendout
   jspawn binary_mainbody
   jspawn C_opt_customhdr
   jsync

   # Operational basics with trivial tests
   jspawn alias
   jspawn charsetalias
   jspawn shortcut
   jspawn netrc
   jsync

   # Operational basics with easy tests
   jspawn expandaddr # (after t_alias)
   jspawn mta_aliases # (after t_expandaddr)
   jspawn filetype
   jspawn e_H_L_opts
   jspawn newmail_on_folder
   jspawn q_t_etc_opts
   jspawn message_injections
   jspawn attachments
   jspawn rfc2231 # (after attachments)
   jspawn mimetype
   jspawn mime_types_load_control
   jsync

   # Around state machine, after basics
   jspawn alternates
   jspawn cmd_escapes
   jspawn compose_edits
   jspawn digmsg
   jspawn on_main_loop_tick
   jspawn on_program_exit
   jsync

   # Heavy use of/rely on state machine (behaviour) and basics
   jspawn compose_hooks
   jspawn mass_recipients
   jspawn lreply_futh_rth_etc
   jspawn pipe_handlers
   jspawn mailcap
   jsync

   # Unclassified rest
   jspawn top
   jspawn z
   jsync

   # OPT_TLS (basics, like S/MIME)
   jspawn s_mime
   jsync

   ## OPT_NET_TEST -> major switch $TESTS_NET_TEST as below
   jspawn net_pop3
   jspawn net_imap
   jspawn net_smtp
   jsync

   jsync 1
} #}}}

# Running{{{
ssec=$SECONDS
if [ -z "${CHECK}${RUN_TEST}" ]; then
   jobs_max
   cc_all_configs
else
   if have_feat debug; then
      if have_feat devel; then
         JOBSYNC=1
         DEVELDIFF=y
         DUMPERR=y
         ARGS="${ARGS} -Smemdebug"
         JOBWAIT=`add $JOBWAIT $JOBWAIT`
      fi
   elif have_feat devel; then
      DEVELDIFF=y
      DUMPERR=y
      LOOPS_MAX=${LOOPS_BIG}
   fi
   color_init

   if [ -z "${RUN_TEST}" ] || [ ${#} -eq 0 ]; then
      jobs_max
      printf 'Will do up to %s tests in parallel, with a %s second timeout\n' \
         ${JOBNO} ${JOBWAIT}
      jobreaper_start
      t_all
      jobreaper_stop
   else
      JOBNO=1
      printf 'Tests have a %s second timeout\n' ${JOBWAIT}
      jobreaper_start
      while [ ${#} -gt 0 ]; do
         jspawn ${1}
         shift
      done
      jobreaper_stop
   fi

fi
esec=$SECONDS

printf '%u tests: %s%u ok%s, %s%u failure(s)%s.  %s%u test(s) skipped%s\n' \
   "${TESTS_PERFORMED}" "${COLOR_OK_ON}" "${TESTS_OK}" "${COLOR_OK_OFF}" \
   "${COLOR_ERR_ON}" "${TESTS_FAILED}" "${COLOR_ERR_OFF}" \
   "${COLOR_WARN_ON}" "${TESTS_SKIPPED}" "${COLOR_WARN_OFF}"
if [ -n "${ssec}" ] && [ -n "${esec}" ]; then
   ( echo 'Elapsed seconds: '`$awk 'BEGIN{print '"${esec}"' - '"${ssec}"'}'` )
fi
#}}}

exit ${ESTAT}
# s-sh-mode
