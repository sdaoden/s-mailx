#!/bin/sh -
#@ Synopsis: ./cc-test.sh [--check-only s-mailx-binary]
#@           ./cc-test.sh --mae-test s-mailx-binary [:TESTNAME:]
#@ The latter generates output files.
#@ TODO _All_ the tests should happen in a temporary subdir.
# Public Domain

# Instead of figuring out the environment in here, require a configured build
# system and include that!  Our makefile and configure ensure that this test
# does not run in the configured, but the user environment nonetheless!
if [ -f ./mk-config.ev ]; then
   . ./mk-config.ev
   if [ -z "${MAILX__CC_TEST_RUNNING}" ]; then
      MAILX__CC_TEST_RUNNING=1
      export MAILX__CC_TEST_RUNNING
      exec "${SHELL}" "${0}" "${@}"
   fi
else
   echo >&2 'S-nail/S-mailx is not configured.'
   echo >&2 'This test script requires the shell environment that only the'
   echo >&2 'configuration script can figure out, even if it will be used to'
   echo >&2 'test a different binary than the one that would be produced!'
   exit 41
fi

# We need *stealthmua* regardless of $SOURCE_DATE_EPOCH, the program name as
# such is a compile-time variable
ARGS='-:/ -# -Sdotlock-ignore-error -Sexpandaddr=restrict'
   ARGS="${ARGS}"' -Smime-encoding=quoted-printable -Snosave -Sstealthmua'
ADDARG_UNI=-Sttycharset=UTF-8
CONF=./make.rc
BODY=./.cc-body.txt
MBOX=./.cc-test.mbox
MAIL=/dev/null
#UTF8_LOCALE= autodetected unless set

# Note valgrind has problems with FDs in forked childs, which causes some tests
# to fail (the FD is rewound and thus will be dumped twice)
MEMTESTER=
#MEMTESTER='valgrind --leak-check=full --log-file=.vl-%p '

##  -- (>8  --  8<)  --  ##

( set -o noglob ) >/dev/null 2>&1 && noglob_shell=1 || unset noglob_shell

msg() {
   fmt=${1}
   shift
   printf >&2 -- "${fmt}\\n" "${@}"
}

##  --  >8  --  8<  --  ##

export ARGS ADDARG_UNI CONF BODY MBOX MAIL  MAKE awk cat cksum rm sed grep

LC_ALL=C LANG=C
TZ=UTC
# Wed Oct  2 01:50:07 UTC 1996
SOURCE_DATE_EPOCH=844221007

export LC_ALL LANG TZ SOURCE_DATE_EPOCH
unset POSIXLY_CORRECT LOGNAME USER

usage() {
   echo >&2 "Synopsis: ./cc-test.sh [--check-only s-mailx-binary]"
   echo >&2 "Synopsis: ./cc-test.sh --mae-test s-mailx-binary [:TESTNAME:]"
   exit 1
}

CHECK_ONLY= MAE_TEST= MAILX=
if [ "${1}" = --check-only ]; then
   CHECK_ONLY=1
   MAILX=${2}
   [ -x "${MAILX}" ] || usage
   shift 2
elif [ "${1}" = --mae-test ]; then
   MAE_TEST=1
   MAILX=${2}
   [ -x "${MAILX}" ] || usage
   shift 2
fi
RAWMAILX=${MAILX}
MAILX="${MEMTESTER}${MAILX}"
export RAWMAILX MAILX

if [ -n "${CHECK_ONLY}${MAE_TEST}" ] && [ -z "${UTF8_LOCALE}" ]; then
   # Try ourselfs for nl_langinfo(CODESET) output first (requires a new version)
   i=`LC_ALL=C.utf8 "${RAWMAILX}" ${ARGS} -X '
      \define cset_test {
         \if [ "${ttycharset}" @i=% utf ]
            \echo $LC_ALL
            \xit 0
         \end
         \if [ "${#}" -gt 0 ]
            \wysh set LC_ALL=${1}
            \shift
            \eval xcall cset_test "${@}"
         \end
         \xit 1
      }
      \call cset_test C.UTF-8 POSIX.utf8 POSIX.UTF-8 en_EN.utf8 en_EN.UTF-8 \
         en_US.utf8 en_US.UTF-8
   '`
   [ $? -eq 0 ] && UTF8_LOCALE=$i

   if [ -z "${UTF8_LOCALE}" ] && (locale yesexpr) >/dev/null 2>&1; then
      UTF8_LOCALE=`locale -a | { m=
         while read n; do
            if { echo ${n} | ${grep} -i 'utf-\{0,1\}8'; } >/dev/null 2>&1; then
               m=${n}
               if { echo ${n} | ${grep} -e POSIX -e en_EN -e en_US; }; then
                  exit 0
               fi
            fi
            m=${n}
         done
         echo ${m}
      }`
   fi
fi

ESTAT=0

TRAP_EXIT_ADDONS=
trap "${rm} -rf \"${BODY}\" \"${MBOX}\" \${TRAP_EXIT_ADDONS}" EXIT
trap "exit 1" HUP INT TERM

# cc_all_configs()
# Test all configs TODO doesn't cover all *combinations*, stupid!
cc_all_configs() {
   < ${CONF} ${awk} '
      BEGIN {
         NOTME["OPT_AUTOCC"] = 1
         NOTME["OPT_DEBUG"] = 1
         NOTME["OPT_DEVEL"] = 1
         NOTME["OPT_NOEXTMD5"] = 1
         NOTME["OPT_ASAN_ADDRESS"] = 1
         NOTME["OPT_ASAN_MEMORY"] = 1
         NOTME["OPT_FORCED_STACKPROT"] = 1
         NOTME["OPT_NOMEMDBG"] = 1
         NOTME["OPT_NYD2"] = 1
         i = 0
      }
      /^[[:space:]]*OPT_/ {
         sub(/^[[:space:]]*/, "")
         # This bails for UnixWare 7.1.4 awk(1), but preceeding = with \
         # does not seem to be a compliant escape for =
         #sub(/=.*$/, "")
         $1 = substr($1, 1, index($1, "=") - 1)
         if (NOTME[$1])
            next
         data[i++] = $1
      }
      END {
         # Doing this completely sequentially and not doing make distclean in
         # between runs should effectively result in lesser compilations.
         # It is completely dumb nonetheless... TODO
         for (j = 1; j < i; ++j) {
            for (k = 1; k < j; ++k)
               printf data[k] "=1 "
            for (k = j; k < i; ++k)
               printf data[k] "=0 "
            printf "OPT_AUTOCC=1\n"
         }
         for (j = 1; j < i; ++j) {
            for (k = 1; k < j; ++k)
               printf data[k] "=0 "
            for (k = j; k < i; ++k)
               printf data[k] "=1 "
            printf "OPT_AUTOCC=1\n"
         }
         # With debug
         for (j = 1; j < i; ++j) {
            for (k = 1; k < j; ++k)
               printf data[k] "=1 "
            for (k = j; k < i; ++k)
               printf data[k] "=0 "
            printf "OPT_AUTOCC=1\n"
            printf "OPT_DEBUG=1\n"
         }
         for (j = 1; j < i; ++j) {
            for (k = 1; k < j; ++k)
               printf data[k] "=0 "
            for (k = j; k < i; ++k)
               printf data[k] "=1 "
            printf "OPT_AUTOCC=1\n"
            printf "OPT_DEBUG=1\n"
         }

         printf "CONFIG=NULL OPT_AUTOCC=0\n"
         printf "CONFIG=NULL OPT_AUTOCC=1\n"
         printf "CONFIG=NULLI OPT_AUTOCC=0\n"
         printf "CONFIG=NULLI OPT_AUTOCC=1\n"
         printf "CONFIG=MINIMAL OPT_AUTOCC=0\n"
         printf "CONFIG=MINIMAL OPT_AUTOCC=1\n"
         printf "CONFIG=NETSEND OPT_AUTOCC=0\n"
         printf "CONFIG=NETSEND OPT_AUTOCC=1\n"
         printf "CONFIG=MAXIMAL OPT_AUTOCC=0\n"
         printf "CONFIG=MAXIMAL OPT_AUTOCC=1\n"
         printf "CONFIG=DEVEL OPT_AUTOCC=0\n"
         printf "CONFIG=DEVEL OPT_AUTOCC=1\n"
         printf "CONFIG=ODEVEL OPT_AUTOCC=0\n"
         printf "CONFIG=ODEVEL OPT_AUTOCC=1\n"
      }
   ' | while read c; do
      printf "\n\n##########\n$c\n"
      printf "\n\n##########\n$c\n" >&2
      sh -c "${MAKE} ${c} all test"
   done
   ${MAKE} distclean
}

have_feat() {
   ( "${RAWMAILX}" ${ARGS} -X'echo $features' -Xx |
      ${grep} +${1} ) >/dev/null 2>&1
}

t_prolog() {
   ${rm} -rf "${BODY}" "${MBOX}" ${TRAP_EXIT_ADDONS}
   TRAP_EXIT_ADDONS=
   [ ${#} -gt 0 ] && printf '[%s]\n' "${1}"
}
t_epilog() {
   t_prolog
}

check() {
   restat=${?} tid=${1} eestat=${2} f=${3} s=${4}
   [ "${eestat}" != - ] && [ "${restat}" != "${eestat}" ] &&
      err "${tid}" 'unexpected exit status: '"${restat} != ${eestat}"
   csum="`${cksum} < ${f}`"
   if [ "${csum}" = "${s}" ]; then
      printf '%s: ok\n' "${tid}"
   else
      ESTAT=1
      printf '%s: error: checksum mismatch (got %s)\n' "${tid}" "${csum}"
   fi
   if [ -n "${MAE_TEST}" ]; then
      x=`echo ${tid} | ${tr} "/:=" "__-"`
      ${cp} -f "${f}" ./mae-test-"${x}"
   fi
}

err() {
   ESTAT=1
   printf '%s: error: %s\n' ${1} "${2}"
}

ex0_test() {
   # $1=test name [$2=status]
   __qm__=${?}
   [ ${#} -gt 1 ] && __qm__=${2}
   if [ ${__qm__} -ne 0 ]; then
      err $1 'unexpected non-0 exit status'
   else
      printf '%s: ok\n' "${1}"
   fi
}

exn0_test() {
   # $1=test name [$2=status]
   __qm__=${?}
   [ ${#} -gt 1 ] && __qm__=${2}
   if [ ${__qm__} -eq 0 ]; then
      err $1 'unexpected 0 exit status'
   else
      printf '%s: ok\n' "${1}"
   fi
}

if ( [ "$((1 + 1))" = 2 ] ) >/dev/null 2>&1; then
   add() {
      echo "$((${1} + ${2}))"
   }
else
   add() {
      echo `${awk} 'BEGIN{print '${1}' + '${2}'}'`
   }
fi

if ( [ "$((2 % 3))" = 2 ] ) >/dev/null 2>&1; then
   modulo() {
      echo "$((${1} % ${2}))"
   }
else
   modulo() {
      echo `${awk} 'BEGIN{print '${1}' % '${2}'}'`
   }
fi

# t_behave()
# Basic (easily testable) behaviour tests
t_behave() {
   t_behave_X_opt_input_command_stack
   t_behave_X_errexit
   t_behave_S_freeze
   t_behave_wysh
   t_behave_input_inject_semicolon_seq
   t_behave_commandalias
   t_behave_ifelse
   t_behave_localopts
   t_behave_local
   t_behave_macro_param_shift
   t_behave_addrcodec
   t_behave_vexpr
   t_behave_call_ret
   t_behave_xcall
   t_behave_vpospar
   t_behave_atxplode
   t_behave_read

   t_behave_mbox
   t_behave_maildir
   t_behave_record_a_resend
   t_behave_e_H_L_opts

   t_behave_alternates
   t_behave_alias
   # FIXME t_behave_mlist
   t_behave_filetype

   t_behave_message_injections
   t_behave_compose_hooks
   t_behave_mass_recipients
   t_behave_mime_types_load_control
   t_behave_lreply_futh_rth_etc

   t_behave_xxxheads_rfc2047
   t_behave_rfc2231
   t_behave_iconv_mbyte_base64
   t_behave_iconv_mainbody
   t_behave_q_t_etc_opts

   t_behave_s_mime
}

t_behave_X_opt_input_command_stack() {
   t_prolog t_behave_X_opt_input_command_stack

   ${cat} <<- '__EOT' > "${BODY}"
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

   # The -X option supports multiline arguments, and those can internally use
   # reverse solidus newline escaping.  And all -X options are joined...
   APO=\'
   < "${BODY}" ${MAILX} ${ARGS} \
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
   ' > "${MBOX}"

   check behave:x_opt_input_command_stack 0 "${MBOX}" '1786542668 416'

   t_epilog
}

t_behave_X_errexit() {
   t_prolog t_behave_X_errexit

   ${cat} <<- '__EOT' > "${BODY}"
	echo one
	echos nono
	echo two
	__EOT

   </dev/null ${MAILX} ${ARGS} -Snomemdebug \
         -X'echo one' -X' echos nono ' -X'echo two' \
      > "${MBOX}" 2>&1
   check behave:x_errexit-1 0 "${MBOX}" '916157812 53'

   </dev/null ${MAILX} ${ARGS} -X'source '"${BODY}" -Snomemdebug \
      > "${MBOX}" 2>&1
   check behave:x_errexit-2 0 "${MBOX}" '916157812 53'

   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u -Snomemdebug \
      > "${MBOX}" 2>&1
   check behave:x_errexit-3 0 "${MBOX}" '916157812 53'

   ##

   </dev/null ${MAILX} ${ARGS} -Serrexit -Snomemdebug \
         -X'echo one' -X' echos nono ' -X'echo two' \
      > "${MBOX}" 2>&1
   check behave:x_errexit-4 1 "${MBOX}" '2118430867 49'

   </dev/null ${MAILX} ${ARGS} -X'source '"${BODY}" -Serrexit -Snomemdebug \
      > "${MBOX}" 2>&1
   check behave:x_errexit-5 1 "${MBOX}" '2118430867 49'

   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u -Serrexit -Snomemdebug \
      > "${MBOX}" 2>&1
   check behave:x_errexit-6 1 "${MBOX}" '12955965 172'

   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u -Sposix -Snomemdebug \
      > "${MBOX}" 2>&1
   check behave:x_errexit-7 1 "${MBOX}" '12955965 172'

   ## Repeat 4-7 with ignerr set

   ${sed} -e 's/^echos /ignerr echos /' < "${BODY}" > "${MBOX}"

   </dev/null ${MAILX} ${ARGS} -Serrexit -Snomemdebug \
         -X'echo one' -X'ignerr echos nono ' -X'echo two' \
      > "${BODY}" 2>&1
   check behave:x_errexit-8 0 "${BODY}" '916157812 53'

   </dev/null ${MAILX} ${ARGS} -X'source '"${MBOX}" -Serrexit -Snomemdebug \
      > "${BODY}" 2>&1
   check behave:x_errexit-9 0 "${BODY}" '916157812 53'

   </dev/null MAILRC="${MBOX}" ${MAILX} ${ARGS} -:u -Serrexit -Snomemdebug \
      > "${BODY}" 2>&1
   check behave:x_errexit-10 0 "${BODY}" '916157812 53'

   </dev/null MAILRC="${MBOX}" ${MAILX} ${ARGS} -:u -Sposix -Snomemdebug \
      > "${BODY}" 2>&1
   check behave:x_errexit-11 0 "${BODY}" '916157812 53'

   t_epilog
}

t_behave_S_freeze() {
   t_prolog t_behave_S_freeze
   oterm=$TERM
   unset TERM

   # Test basic assumption
   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} \
      -X'echo asksub<$asksub> dietcurd<$dietcurd>' \
      -Xx > "${MBOX}" 2>&1
   check behave:s_freeze-1 0 "${MBOX}" '270686329 21'

   #
   ${cat} <<- '__EOT' > "${BODY}"
	echo asksub<$asksub>
	set asksub
	echo asksub<$asksub>
	__EOT
   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u \
      -Snoasksub -Sasksub -Snoasksub \
      -X'echo asksub<$asksub>' -X'set asksub' -X'echo asksub<$asksub>' \
      -Xx > "${MBOX}" 2>&1
   check behave:s_freeze-2 0 "${MBOX}" '3182942628 37'

   ${cat} <<- '__EOT' > "${BODY}"
	echo asksub<$asksub>
	unset asksub
	echo asksub<$asksub>
	__EOT
   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u \
      -Snoasksub -Sasksub \
      -X'echo asksub<$asksub>' -X'unset asksub' -X'echo asksub<$asksub>' \
      -Xx > "${MBOX}" 2>&1
   check behave:s_freeze-3 0 "${MBOX}" '2006554293 39'

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
      -Xx > "${MBOX}" 2>&1
   check behave:s_freeze-4 0 "${MBOX}" '1985768109 65'

   ${cat} <<- '__EOT' > "${BODY}"
	echo dietcurd<$dietcurd>
	unset dietcurd
	echo dietcurd<$dietcurd>
	__EOT
   </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u \
      -Sdietcurd=strawberry -Snodietcurd \
      -X'echo dietcurd<$dietcurd>' -X'set dietcurd=vanilla' \
         -X'echo dietcurd<$dietcurd>' \
      -Xx > "${MBOX}" 2>&1
   check behave:s_freeze-5 0 "${MBOX}" '151574279 51'

   # TODO once we have a detached one with env=1..
   if [ -n "`</dev/null ${MAILX} ${ARGS} -X'!echo \$TERM' -Xx`" ]; then
      echo 'behave:s_freeze-{6,7}: shell sets $TERM, skipped'
   else
      ${cat} <<- '__EOT' > "${BODY}"
		!echo "shell says TERM<$TERM>"
	echo TERM<$TERM>
		!echo "shell says TERM<$TERM>"
	set TERM=cherry
		!echo "shell says TERM<$TERM>"
	echo TERM<$TERM>
		!echo "shell says TERM<$TERM>"
		__EOT
      </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u \
         -STERM=strawberry -SnoTERM -STERM=vanilla \
         -X'echo mail<$TERM>' -X'unset TERM' \
         -X'!echo "shell says TERM<$TERM>"' -X'echo TERM<$TERM>' \
         -Xx > "${MBOX}" 2>&1
   check behave:s_freeze-6 0 "${MBOX}" '1211476036 167'

      ${cat} <<- '__EOT' > "${BODY}"
		!echo "shell says TERM<$TERM>"
	echo TERM<$TERM>
		!echo "shell says TERM<$TERM>"
	set TERM=cherry
		!echo "shell says TERM<$TERM>"
	echo TERM<$TERM>
		!echo "shell says TERM<$TERM>"
	__EOT
      </dev/null MAILRC="${BODY}" ${MAILX} ${ARGS} -:u \
         -STERM=strawberry -SnoTERM \
         -X'echo TERM<$TERM>' -X'set TERM=vanilla' \
         -X'!echo "shell says TERM<$TERM>"' -X'echo TERM<$TERM>' \
         -Xx > "${MBOX}" 2>&1
      check behave:s_freeze-7 0 "${MBOX}" '3365080441 132'
   fi

   TERM=$oterm
   t_epilog
}

t_behave_wysh() {
   t_prolog t_behave_wysh

   ${cat} <<- '__EOT' > "${BODY}"
	#
	echo abcd
	echo a'b'c'd'
	echo a"b"c"d"
	echo a$'b'c$'d'
	echo 'abcd'
	echo "abcd"
	echo $'abcd'
	echo a\ b\ c\ d
	echo a 'b c' d
	echo a "b c" d
	echo a $'b c' d
	#
	echo 'a$`"\'
	echo "a\$\`'\"\\"
	echo $'a\$`\'\"\\'
	echo $'a\$`\'"\\'
	# DIET=CURD TIED=
	echo 'a${DIET}b${TIED}c\${DIET}d\${TIED}e' # COMMENT
	echo "a${DIET}b${TIED}c\${DIET}d\${TIED}e"
	echo $'a${DIET}b${TIED}c\${DIET}d\${TIED}e'
	#
	echo a$'\101\0101\x41\u0041\u41\U00000041\U41'c
	echo a$'\u0041\u41\u0C1\U00000041\U41'c
	echo a$'\377'c
	echo a$'\0377'c
	echo a$'\400'c
	echo a$'\0400'c
	echo a$'\U1100001'c
	#
	echo a$'b\0c'd
	echo a$'b\00c'de
	echo a$'b\000c'df
	echo a$'b\0000c'dg
	echo a$'b\x0c'dh
	echo a$'b\x00c'di
	echo a$'b\u0'dj
	echo a$'b\u00'dk
	echo a$'b\u000'dl
	echo a$'b\u0000'dm
	echo a$'b\U0'dn
	echo a$'b\U00'do
	echo a$'b\U000'dp
	echo a$'b\U0000'dq
	echo a$'b\U00000'dr
	echo a$'b\U000000'ds
	echo a$'b\U0000000'dt
	echo a$'b\U00000000'du
	#
	echo a$'\cI'b
	echo a$'\011'b
	echo a$'\x9'b
	echo a$'\u9'b
	echo a$'\U9'b
	echo a$'\c@'b c d
	__EOT

   if [ -z "${UTF8_LOCALE}" ]; then
      echo 'Skip behave:wysh_unicode, no UTF8_LOCALE'
   else
      < "${BODY}" DIET=CURD TIED= \
      LC_ALL=${UTF8_LOCALE} ${MAILX} ${ARGS} 2>/dev/null > "${MBOX}"
      check behave:wysh_unicode 0 "${MBOX}" '475805847 317'
   fi

   < "${BODY}" DIET=CURD TIED= ${MAILX} ${ARGS} > "${MBOX}" 2>/dev/null
   check behave:wysh_c 0 "${MBOX}" '1473887148 321'

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}"
   wysh set mager='\hey\'
   varshow mager
   wysh set mager="\hey\\"
   varshow mager
   wysh set mager=$'\hey\\'
   varshow mager
	__EOT
   check behave:wysh-3 0 "${MBOX}" '1289698238 69'

   t_epilog
}

t_behave_input_inject_semicolon_seq() {
   t_prolog t_behave_input_inject_semicolon_seq

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}"
	define mydeepmac {
		echon '(mydeepmac)';
	}
	define mymac {
		echon this_is_mymac;call mydeepmac;echon ';';
	}
	echon one';';call mymac;echon two";";call mymac;echo three$';';
	define mymac {
		echon this_is_mymac;call mydeepmac;echon ,TOO'!;';
	}
	echon one';';call mymac;echon two";";call mymac;echo three$';';
	__EOT

   check behave:input_inject_semicolon_seq 0 "${MBOX}" '512117110 140'

   t_epilog
}

t_behave_commandalias() {
   t_prolog t_behave_commandalias

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}"
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

   check behave:commandalias 0 "${MBOX}" '3694143612 31'

   t_epilog
}

t_behave_ifelse() {
   t_prolog t_behave_ifelse

   # Nestable conditions test
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}"
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
		if $dietcurd
		   echo 3.err
		else
		   echo 3.ok
		endif
		set dietcurd=yoho
		if $dietcurd
		   echo 4.ok
		else
		   echo 4.err
		endif
		if $dietcurd == 'yoho'
		   echo 5.ok
		else
		   echo 5.err
		endif
		if $dietcurd @== 'Yoho'
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
		if $dietcurd @!= 'Yoho'
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
		   if $dietcurd -le 9
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
		   if $dietcurd @> abB
		      echo 12.ok2
		   else
		      echo 12.err2
		   endif
		   if $dietcurd @== aBC
		      echo 12.ok3
		   else
		      echo 12.err3
		   endif
		   if $dietcurd @>= AbC
		      echo 12.ok4
		   else
		      echo 12.err4
		   endif
		   if $dietcurd @<= ABc
		      echo 12.ok5
		   else
		      echo 12.err5
		   endif
		   if $dietcurd @>= abd
		      echo 12.err6
		   else
		      echo 12.ok6
		   endif
		   if $dietcurd @<= abb
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
      if $dietcurd @< aBc
         echo 12-2.err
      else
         echo 12-2.ok
      endif
      if $dietcurd > ABc
         echo 12-3.ok
      else
         echo 12-3.err
      endif
      if $dietcurd @> ABc
         echo 12-3.err
      else
         echo 12-3.ok
      endif
		if $dietcurd @i=% aB
		   echo 13.ok
		else
		   echo 13.err
		endif
		if $dietcurd =% aB
		   echo 13-1.err
		else
		   echo 13-1.ok
		endif
		if $dietcurd @=% bC
		   echo 14.ok
		else
		   echo 14.err
		endif
		if $dietcurd !% aB
		   echo 15-1.ok
		else
		   echo 15-1.err
		endif
		if $dietcurd @!% aB
		   echo 15-2.err
		else
		   echo 15-2.ok
		endif
		if $dietcurd !% bC
		   echo 15-3.ok
		else
		   echo 15-3.err
		endif
		if $dietcurd @!% bC
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
		set diet=abc curd=abc
		if $diet == $curd
		   echo 18.ok
		else
		   echo 18.err
		endif
		set diet=abc curd=abcd
		if $diet != $curd
		   echo 19.ok
		else
		   echo 19.err
		endif
		# 1. Shitty grouping capabilities as of today
		unset diet curd ndefined
		if [ [ false ] || [ false ] || [ true ] ] && [ [ false ] || [ true ] ] && \
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
		if [ [ $diet == 'yo' ] && [ $curd == 'ho' ] ] && [ $ndefined ]
		   echo 36.err
		else
		   echo 36.ok
		endif
		set ndefined
		if [ [ $diet == 'yo' ] && [ $curd == 'ho' ] ] && [ $ndefined ]
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
		if [ $diet == 'yo' && $curd == 'ho' ] && $ndefined
		   echo 56.err
		else
		   echo 56.ok
		endif
		if $diet == 'yo' && $curd == 'ho' && $ndefined
		   echo 57.err
		else
		   echo 57.ok
		endif
		set ndefined
		if [ $diet == 'yo' && $curd == 'ho' ] && $ndefined
		   echo 57.ok
		else
		   echo 57.err
		endif
		if $diet == 'yo' && $curd == 'ho' && $ndefined
		   echo 58.ok
		else
		   echo 58.err
		endif
		if [ [ [ [ [ [ $diet == 'yo' && $curd == 'ho' && $ndefined ] ] ] ] ] ]
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
		if ${diet} != ${curd}
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

   check behave:if-normal 0 "${MBOX}" '1688759742 719'

   if have_feat regex; then
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}"
			set dietcurd=yoho
			if $dietcurd =~ '^yo.*'
			   echo 1.ok
			else
			   echo 1.err
			endif
			if $dietcurd =~ '^Yo.*'
			   echo 1-1.err
			else
			   echo 1-1.ok
			endif
			if $dietcurd @=~ '^Yo.*'
			   echo 1-2.ok
			else
			   echo 1-2.err
			endif
			if $dietcurd =~ '^yOho.+'
			   echo 2.err
			else
			   echo 2.ok
			endif
			if $dietcurd @!~ '.*Ho$'
			   echo 3.err
			else
			   echo 3.ok
			endif
			if $dietcurd !~ '.+yohO$'
			   echo 4.ok
			else
			   echo 4.err
			endif
			if [ $dietcurd @i!~ '.+yoho$' ]
			   echo 5.ok
			else
			   echo 5.err
			endif
			if ! [ $dietcurd @i=~ '.+yoho$' ]
			   echo 6.ok
			else
			   echo 6.err
			endif
			if ! ! [ $dietcurd @i!~ '.+yoho$' ]
			   echo 7.ok
			else
			   echo 7.err
			endif
			if ! [ ! [ $dietcurd @i!~ '.+yoho$' ] ]
			   echo 8.ok
			else
			   echo 8.err
			endif
			if [ ! [ ! [ $dietcurd @i!~ '.+yoho$' ] ] ]
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
			if $diet !~ $curd
			   echo 15.ok
			else
			   echo 15.err
			endif
		__EOT

      check behave:if-regex 0 "${MBOX}" '1115671789 95'
   else
      printf 'behave:if-regex: unsupported, skipped\n'
   fi

   t_epilog
}

t_behave_localopts() {
   t_prolog t_behave_localopts

   # Nestable conditions test
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
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
      wysh set y=$1; shift; eval localopts $y; localopts $1; shift
      set x=1
      echo ll1.1=$x
      call ll2 $1
      echo ll1.2=$x
   }
   define ll0 {
      wysh set y=$1; shift; eval localopts $y; localopts $1; shift
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

   check behave:localopts 0 "${MBOX}" '4016155249 1246'

   t_epilog
}

t_behave_local() {
   t_prolog t_behave_local

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
	define du2 {
	   echo du2-1 du=$du
	   local set du=$1
	   echo du2-2 du=$du
	   local unset du
	   echo du2-3 du=$du
	}
	define du {
	   local set du=dudu
	   echo du-1 du=$du
	   call du2 du2du2
	   echo du-2 du=$du
	   local set nodu
	   echo du-3 du=$du
	}
	define ich {
	   echo ich-1 du=$du
	   call du
	   echo ich-2 du=$du
	}
	define wir {
	   localopts $1
	   set du=wirwir
	   echo wir-1 du=$du
	   call ich
	   echo wir-2 du=$du
	}
	echo ------- global-1 du=$du
	call ich
	echo ------- global-2 du=$du
	set du=global
	call ich
	echo ------- global-3 du=$du
	call wir on
	echo ------- global-4 du=$du
	call wir off
	echo ------- global-5 du=$du
	__EOT

   check behave:local-1 0 "${MBOX}" '2411598140 641'

   t_epilog
}

t_behave_macro_param_shift() {
   t_prolog t_behave_macro_param_shift

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>/dev/null
	define t2 {
	   echo in: t2
	   echo t2.0 has $#/${#} parameters: "$1,${2},$3" (${*}) [$@]
	   localopts on
	   wysh set ignerr=$1
	   shift
	   localopts off
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

   check behave:macro_param_shift 0 "${MBOX}" '1402489146 1682'

   t_epilog
}

t_behave_addrcodec() {
   t_prolog t_behave_addrcodec

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
	vput addrcodec res e 1 <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res e 2 . <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res e 3 Sauer Dr. <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res e 3.50 Sauer (Ma) Dr. <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res e 3.51 Sauer (Ma) "Dr." <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	#
	vput addrcodec res +e 4 Sauer (Ma) Dr. <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 5 Sauer (Ma) Braten Dr. <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 6 Sauer (Ma) Braten Dr. (Heu) <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 7 Sauer (Ma) Braten Dr. (Heu) <doog@def> (bu)
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 8 \
		Dr. Sauer (Ma) Braten Dr. (Heu) <doog@def> (bu) Boom. Boom
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 9 Dr.Sauer(Ma)Braten Dr. (Heu) <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 10 (Ma)Braten Dr. (Heu) <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 11 (Ma)Braten Dr"." (Heu) <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 12 Dr.     Sauer  (Ma)   Braten    Dr.   (u) <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 13(Ma)Braten    Dr.     (Heu)     <doog@def>
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 14 Hey, Du <doog@def> Wie() findet Dr. das? ()
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 15 \
		Hey, Du <doog@def> Wie() findet "" Dr. "" das? ()
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 16 \
		"Hey," "Du" <doog@def> "Wie()" findet "" Dr. "" das? ()
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 17 \
		"Hey" Du <doog@def> "Wie() findet " " Dr. """ das? ()
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 18 \
		<doog@def> "Hey" Du "Wie() findet " " Dr. """ das? ()
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res +e 19 Hey\,\"  <doog@def> "Wie()" findet \" Dr. \" das?
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	#
	vput addrcodec res ++e 20 Hey\,\"  <doog@def> "Wie()" findet \" Dr. \" das?
	echo $?/$^ERRNAME $res
	vput addrcodec res ++e 21 Hey\,\""  <doog@def> "Wie()" findet \" Dr. \" das?
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	#
	vput addrcodec res +++e 22 Hey\\,\"  <doog@def> "Wie()" findet \" Dr. \" das?
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	#
	vput addrcodec res s \
		"23 Hey\\,\\\" \"Wie" () "\" findet \\\" Dr. \\\" das?" <doog@def>
	echo $?/$^ERRNAME $res
	#
	# Fix for [f3852f88]
	vput addrcodec res ++e <from2@exam.ple> 100 (comment) "Quot(e)d"
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	vput addrcodec res e <from2@exam.ple> 100 (comment) "Quot(e)d"
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	__EOT

   check behave:addrcodec-1 0 "${MBOX}" '1047317989 2612'

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
   mlist isa1@list
   mlsubscribe isa2@list
   #
   vput addrcodec res skin Hey\\,\"  <isa0@list> "Wie()" find \" Dr. \" das?
   echo $?/$^ERRNAME $res
   vput addrcodec res skinlist Hey\\,\"  <isa0@list> "Wie()" find \" Dr. \" das?
   echo $?/$^ERRNAME $res
   vput addrcodec res skin Hey\\,\"  <isa1@list> "Wie()" find \" Dr. \" das?
   echo $?/$^ERRNAME $res
   vput addrcodec res skinlist Hey\\,\"  <isa1@list> "Wie()" find \" Dr. \" das?
   echo $?/$^ERRNAME $res
   vput addrcodec res skin Hey\\,\"  <isa2@list> "Wie()" find \" Dr. \" das?
   echo $?/$^ERRNAME $res
   vput addrcodec res skinlist Hey\\,\"  <isa2@list> "Wie()" find \" Dr. \" das?
   echo $?/$^ERRNAME $res
	__EOT

   check behave:addrcodec-2 0 "${MBOX}" '1391779299 104'

   if have_feat idna; then
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} ${ADDARG_UNI} > "${MBOX}" 2>&1
      vput addrcodec res e    (heu) <du@blödiän> "stroh" du   
      echo $?/$^ERRNAME $res
      eval vput addrcodec res d $res
      echo $?/$^ERRNAME $res
      vput addrcodec res e       <du@blödiän>   du     
      echo $?/$^ERRNAME $res
      eval vput addrcodec res d $res
      echo $?/$^ERRNAME $res
      vput addrcodec res e     du    <du@blödiän>   
      echo $?/$^ERRNAME $res
      eval vput addrcodec res d $res
      echo $?/$^ERRNAME $res
      vput addrcodec res e        <du@blödiän>    
      echo $?/$^ERRNAME $res
      eval vput addrcodec res d $res
      echo $?/$^ERRNAME $res
      vput addrcodec res e        du@blödiän    
      echo $?/$^ERRNAME $res
      eval vput addrcodec res d $res
      echo $?/$^ERRNAME $res
		__EOT

      check behave:addrcodec-idna 0 "${MBOX}" '498775983 326'
   else
      printf 'behave:addrcodec-idna: unsupported, skipped\n'
   fi

   t_epilog
}

t_behave_vexpr() {
   t_prolog t_behave_vexpr

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>/dev/null
	vput vexpr res = 9223372036854775807
	echo $?/$^ERRNAME $res
	vput vexpr res = 9223372036854775808
	echo $?/$^ERRNAME $res
	vput vexpr res =@ 9223372036854775808
	echo $?/$^ERRNAME $res
	vput vexpr res = -9223372036854775808
	echo $?/$^ERRNAME $res
	vput vexpr res = -9223372036854775809
	echo $?/$^ERRNAME $res
	vput vexpr res =@ -9223372036854775809
	echo $?/$^ERRNAME $res
	echo ' #1'
	vput vexpr res ~ 0
	echo $?/$^ERRNAME $res
	vput vexpr res ~ 1
	echo $?/$^ERRNAME $res
	vput vexpr res ~ -1
	echo $?/$^ERRNAME $res
	echo ' #2'
	vput vexpr res + 0 0
	echo $?/$^ERRNAME $res
	vput vexpr res + 0 1
	echo $?/$^ERRNAME $res
	vput vexpr res + 1 1
	echo $?/$^ERRNAME $res
	echo ' #3'
	vput vexpr res + 9223372036854775807 0
	echo $?/$^ERRNAME $res
	vput vexpr res + 9223372036854775807 1
	echo $?/$^ERRNAME $res
	vput vexpr res +@ 9223372036854775807 1
	echo $?/$^ERRNAME $res
	vput vexpr res + 0 9223372036854775807
	echo $?/$^ERRNAME $res
	vput vexpr res + 1 9223372036854775807
	echo $?/$^ERRNAME $res
	vput vexpr res +@ 1 9223372036854775807
	echo $?/$^ERRNAME $res
	echo ' #4'
	vput vexpr res + -9223372036854775808 0
	echo $?/$^ERRNAME $res
	vput vexpr res + -9223372036854775808 -1
	echo $?/$^ERRNAME $res
	vput vexpr res +@ -9223372036854775808 -1
	echo $?/$^ERRNAME $res
	vput vexpr res + 0 -9223372036854775808
	echo $?/$^ERRNAME $res
	vput vexpr res + -1 -9223372036854775808
	echo $?/$^ERRNAME $res
	vput vexpr res +@ -1 -9223372036854775808
	echo $?/$^ERRNAME $res
	echo ' #5'
	vput vexpr res - 0 0
	echo $?/$^ERRNAME $res
	vput vexpr res - 0 1
	echo $?/$^ERRNAME $res
	vput vexpr res - 1 1
	echo $?/$^ERRNAME $res
	echo ' #6'
	vput vexpr res - 9223372036854775807 0
	echo $?/$^ERRNAME $res
	vput vexpr res - 9223372036854775807 -1
	echo $?/$^ERRNAME $res
	vput vexpr res -@ 9223372036854775807 -1
	echo $?/$^ERRNAME $res
	vput vexpr res - 0 9223372036854775807
	echo $?/$^ERRNAME $res
	vput vexpr res - -1 9223372036854775807
	echo $?/$^ERRNAME $res
	vput vexpr res - -2 9223372036854775807
	echo $?/$^ERRNAME $res
	vput vexpr res -@ -2 9223372036854775807
	echo $?/$^ERRNAME $res
	echo ' #7'
	vput vexpr res - -9223372036854775808 +0
	echo $?/$^ERRNAME $res
	vput vexpr res - -9223372036854775808 +1
	echo $?/$^ERRNAME $res
	vput vexpr res -@ -9223372036854775808 +1
	echo $?/$^ERRNAME $res
	vput vexpr res - 0 -9223372036854775808
	echo $?/$^ERRNAME $res
	vput vexpr res - +1 -9223372036854775808
	echo $?/$^ERRNAME $res
	vput vexpr res -@ +1 -9223372036854775808
	echo $?/$^ERRNAME $res
	echo ' #8'
	vput vexpr res + -13 -2
	echo $?/$^ERRNAME $res
	vput vexpr res - 0 0
	echo $?/$^ERRNAME $res
	vput vexpr res - 0 1
	echo $?/$^ERRNAME $res
	vput vexpr res - 1 1
	echo $?/$^ERRNAME $res
	vput vexpr res - -13 -2
	echo $?/$^ERRNAME $res
	echo ' #9'
	vput vexpr res * 0 0
	echo $?/$^ERRNAME $res
	vput vexpr res * 0 1
	echo $?/$^ERRNAME $res
	vput vexpr res * 1 1
	echo $?/$^ERRNAME $res
	vput vexpr res * -13 -2
	echo $?/$^ERRNAME $res
	echo ' #10'
	vput vexpr res / 0 0
	echo $?/$^ERRNAME $res
	vput vexpr res / 0 1
	echo $?/$^ERRNAME $res
	vput vexpr res / 1 1
	echo $?/$^ERRNAME $res
	vput vexpr res / -13 -2
	echo $?/$^ERRNAME $res
	echo ' #11'
	vput vexpr res % 0 0
	echo $?/$^ERRNAME $res
	vput vexpr res % 0 1
	echo $?/$^ERRNAME $res
	vput vexpr res % 1 1
	echo $?/$^ERRNAME $res
	vput vexpr res % -13 -2
	echo $?/$^ERRNAME $res
	__EOT

   check behave:vexpr-numeric 0 "${MBOX}" '1723609217 1048'

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
	vput vexpr res find 'bananarama' 'nana'
	echo $?/$^ERRNAME :$res:
	vput vexpr res find 'bananarama' 'bana'
	echo $?/$^ERRNAME :$res:
	vput vexpr res find 'bananarama' 'Bana'
	echo $?/$^ERRNAME :$res:
	vput vexpr res find 'bananarama' 'rama'
	echo $?/$^ERRNAME :$res:
	echo ' #1'
	vput vexpr res ifind 'bananarama' 'nana'
	echo $?/$^ERRNAME :$res:
	vput vexpr res ifind 'bananarama' 'bana'
	echo $?/$^ERRNAME :$res:
	vput vexpr res ifind 'bananarama' 'Bana'
	echo $?/$^ERRNAME :$res:
	vput vexpr res ifind 'bananarama' 'rama'
	echo $?/$^ERRNAME :$res:
	echo ' #2'
	vput vexpr res substring 'bananarama' 1
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 5
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 7
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 9
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 10
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 1 3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 3 3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 5 3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 7 3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 9 3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 10 3
	echo $?/$^ERRNAME :$res:
	echo ' #3'
	vput vexpr res substring 'bananarama' -1
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' -3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' -5
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' -7
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' -9
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' -10
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 1 -3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 3 -3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 5 -3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 7 -3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 9 -3
	echo $?/$^ERRNAME :$res:
	vput vexpr res substring 'bananarama' 10 -3
	echo $?/$^ERRNAME :$res:
	echo ' #4'
	vput vexpr res trim 'Cocoon  Cocoon'
	echo $?/$^ERRNAME :$res:
	vput vexpr res trim '  Cocoon  Cocoon 	  '
	echo $?/$^ERRNAME :$res:
	vput vexpr res trim-front 'Cocoon  Cocoon'
	echo $?/$^ERRNAME :$res:
	vput vexpr res trim-front '  Cocoon  Cocoon 	  '
	echo $?/$^ERRNAME :$res:
	vput vexpr res trim-end 'Cocoon  Cocoon'
	echo $?/$^ERRNAME :$res:
	vput vexpr res trim-end '  Cocoon  Cocoon 	  '
	echo $?/$^ERRNAME :$res:
	__EOT

   check behave:vexpr-string 0 "${MBOX}" '3182004322 601'

   if have_feat regex; then
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
		vput vexpr res regex 'bananarama' 'nana'
		echo $?/$^ERRNAME :$res:
		vput vexpr res regex 'bananarama' 'bana'
		echo $?/$^ERRNAME :$res:
		vput vexpr res regex 'bananarama' 'Bana'
		echo $?/$^ERRNAME :$res:
		vput vexpr res regex 'bananarama' 'rama'
		echo $?/$^ERRNAME :$res:
		echo ' #1'
		vput vexpr res iregex 'bananarama' 'nana'
		echo $?/$^ERRNAME :$res:
		vput vexpr res iregex 'bananarama' 'bana'
		echo $?/$^ERRNAME :$res:
		vput vexpr res iregex 'bananarama' 'Bana'
		echo $?/$^ERRNAME :$res:
		vput vexpr res iregex 'bananarama' 'rama'
		echo $?/$^ERRNAME :$res:
		echo ' #2'
		vput vexpr res regex 'bananarama' '(.*)nana(.*)' '\${1}a\${0}u{\$2}'
		echo $?/$^ERRNAME :$res:
		vput vexpr res regex 'bananarama' '(.*)bana(.*)' '\${1}a\${0}u\$2'
		echo $?/$^ERRNAME :$res:
		vput vexpr res regex 'bananarama' 'Bana(.+)' '\$1\$0'
		echo $?/$^ERRNAME :$res:
		vput vexpr res regex 'bananarama' '(.+)rama' '\$1\$0'
		echo $?/$^ERRNAME :$res:
		echo ' #3'
		vput vexpr res iregex 'bananarama' '(.*)nana(.*)' '\${1}a\${0}u{\$2}'
		echo $?/$^ERRNAME :$res:
		vput vexpr res iregex 'bananarama' '(.*)bana(.*)' '\${1}a\${0}u\$2'
		echo $?/$^ERRNAME :$res:
		vput vexpr res iregex 'bananarama' 'Bana(.+)' '\$1\$0'
		echo $?/$^ERRNAME :$res:
		vput vexpr res iregex 'bananarama' '(.+)rama' '\$1\$0'
		echo $?/$^ERRNAME :$res:
		echo ' #4'
		__EOT

      check behave:vexpr-regex 0 "${MBOX}" '3270360157 311'
   else
      printf 'behave:vexpr-regex: unsupported, skipped\n'
   fi

   t_epilog
}

t_behave_call_ret() {
   t_prolog t_behave_call_ret

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -Snomemdebug > "${MBOX}" 2>&1
	define w1 {
		echon ">$1 "
		vput vexpr i + $1 1
		if [ $i -le 42 ]
			vput vexpr j '&' $i 7
			if [ $j -eq 7 ]
				echo .
			end
			call w1 $i
			wysh set i=$? k=$!
			vput vexpr j '&' $i 7
			echon "<$1/$i/$k "
			if [ $j -eq 7 ]
				echo .
			end
		else
			echo ! The end for $1
		end
		return $1
	}
	# Transport $?/$! up the call chain
	define w2 {
		echon ">$1 "
		vput vexpr i + $1 1
		if [ $1 -lt 42 ]
			call w2 $i
			wysh set i=$? j=$! k=$^ERRNAME
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
		vput vexpr i + $1 1
		if [ $1 -lt 42 ]
			call w3 $i $2
			wysh set i=$? j=$!
			vput vexpr k - $1 $2
			if [ $k -eq 21 ]
				vput vexpr i + $1 1
				vput vexpr j + $2 1
				echo "# <$i/$j> .. "
				call w3 $i $j
				wysh set i=$? j=$!
			end
			eval echon "<\$1=\$i/\$^ERRNAME-$j "
			return $i $j
		else
			echo ! The end for $1=$i/$2
         if [ "$2" != "" ]
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

   check behave:call_ret 0 "${MBOX}" '1572045517 5922'

   t_epilog
}

t_behave_xcall() {
   t_prolog t_behave_xcall

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -Snomemdebug > "${MBOX}" 2>&1
	define work {
		echon "$1 "
		vput vexpr i + $1 1
		if [ $i -le 1111 ]
			vput vexpr j '&' $i 7
			if [ $j -eq 7 ]
				echo .
			end
			\xcall work $i $2
		end
		echo ! The end for $1/$2
		if [ "$2" != "" ]
			return $i $^ERR-BUSY
		end
	}
	define xwork {
		\xcall work 0 $2
	}
	call work 0
	echo ?=$? !=$!
	call xwork
	echo ?=$? !=$!
	xcall xwork
	echo ?=$? !=$^ERRNAME
	#
	call work 0 yes
	echo ?=$? !=$^ERRNAME
	call xwork 0 yes
	echo ?=$? !=$^ERRNAME
	__EOT

   check behave:xcall-1 0 "${MBOX}" '2401702082 23801'

   ##

   ${cat} <<- '__EOT' > "${BODY}"
	define __w {
		echon "$1 "
		vput vexpr i + $1 1
		if [ $i -le 111 ]
			vput vexpr j '&' $i 7
			if [ $j -eq 7 ]
				echo .
			end
			\xcall __w $i $2
		end
		echo ! The end for $1
		if [ $2 -eq 0 ]
			nonexistingcommand
			echo would be err with errexit
			return
		end
		echo calling exit
		exit
	}
	define work {
		echo eins
		call __w 0 0
		echo zwei, ?=$? !=$!
		localopts yes; set errexit
		ignerr call __w 0 0
		echo drei, ?=$? !=$^ERRNAME
		call __w 0 $1
		echo vier, ?=$? !=$^ERRNAME, this is an error
	}
	ignerr call work 0
	echo outer 1, ?=$? !=$^ERRNAME
	xxxign call work 0
	echo outer 2, ?=$? !=$^ERRNAME, could be error if xxxign non-empty
	call work 1
	echo outer 3, ?=$? !=$^ERRNAME
	echo this is definitely an error
	__EOT

   < "${BODY}" ${MAILX} ${ARGS} -X'commandalias xxxign ignerr' -Snomemdebug \
      > "${MBOX}" 2>&1
   check behave:xcall-2 0 "${MBOX}" '3900716531 4200'

   < "${BODY}" ${MAILX} ${ARGS} -X'commandalias xxxign " "' -Snomemdebug \
      > "${MBOX}" 2>&1
   check behave:xcall-3 1 "${MBOX}" '1006776201 2799'

   t_epilog
}

t_behave_vpospar() {
   t_prolog t_behave_vpospar

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
   vpospar set hey, "'you    ", world!
   echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   vput vpospar x quote; echo x<$x>
   vpospar clear;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   vput vpospar y quote;echo y<$y>
   eval vpospar set ${x};echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   eval vpospar set ${y};echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   eval vpospar set ${x};echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>

   define infun2 {
      echo infun2:$?/$^ERRNAME/$#:$*/"$@"/<$1><$2><$3><$4>
      vput vpospar z quote;echo infun2:z<$z>
   }

   define infun {
      echo infun:$?/$^ERRNAME/$#:$*/"$@"/<$1><$2><$3><$4>
      vput vpospar y quote;echo infun:y<$y>
      eval vpospar set ${x};echo infun:$?/$^ERRNAME/$#:$*/"$@"/<$1><$2><$3><$4>
      vpospar clear;echo infun:$?/$^ERRNAME/$#:$*/"$@"/<$1><$2><$3><$4>
      eval call infun2 $x
      echo infun:$?/$^ERRNAME/$#:$*/"$@"/<$1><$2><$3><$4>
      eval vpospar set ${y};echo infun:$?/$^ERRNAME/$#:$*/"$@"/<$1><$2><$3><$4>
   }

   call infun This "in a" fun
   echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   vpospar clear;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
	__EOT
   check behave:vpospar-1 0 "${MBOX}" '155175639 866'

   #
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
   set ifs=\'
   echo ifs<$ifs> ifs-ws<$ifs-ws>
   vpospar set hey, "'you    ", world!
   echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   vput vpospar x quote; echo x<$x>
   vpospar clear;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   eval vpospar set ${x};echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>

   set ifs=,
   echo ifs<$ifs> ifs-ws<$ifs-ws>
   vpospar set hey, "'you    ", world!
   unset ifs;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   set ifs=,
   vput vpospar x quote; echo x<$x>
   vpospar clear;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   eval vpospar set ${x};\
      unset ifs;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>

   wysh set ifs=$',\t'
   echo ifs<$ifs> ifs-ws<$ifs-ws>
   vpospar set hey, "'you    ", world!
   unset ifs; echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   wysh set ifs=$',\t'
   vput vpospar x quote; echo x<$x>
   vpospar clear;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   eval vpospar set ${x};\
   unset ifs;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
	__EOT
   check behave:vpospar-ifs 0 "${MBOX}" '2015927702 706'

   t_epilog
}

t_behave_atxplode() {
   t_prolog t_behave_atxplode
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} > ./.t.sh <<- '___'; ${cat} > ./.t.rc <<- '___'
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
	  if [ $# -gt 0 ]
	    \xcall xxx "$@"
	  endif
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

   ${MAILX} ${ARGS} -X'source ./.t.rc' -Xx > "${MBOX}" 2>&1
   check behave:atxplode-1 0 "${MBOX}" '41566293 164'

   #${SHELL} ./.t.sh > ./.tshout 2>&1
   #check behave:atxplode:disproof-1 0 ./.tshout '41566293 164'

   t_epilog
}

t_behave_read() {
   t_prolog t_behave_read
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<- '__EOT' > .tin
   hey1, "'you    ", world!
   hey2, "'you    ", bugs bunny!
   hey3, "'you    ",     
   hey4, "'you    "
	__EOT

   ${cat} <<- '__EOT' |\
      ${MAILX} ${ARGS} -X'readctl create ./.tin' > "${MBOX}" 2>&1
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   unset a b c;read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   readctl remove ./.tin;echo readctl remove:$?/$^ERRNAME
	__EOT
   check behave:read-1 0 "${MBOX}" '1527910147 173'

   ${cat} <<- '__EOT' > .tin2
   hey2.0,:"'you    ",:world!:mars.:
   hey2.1,:"'you    ",:world!
   hey2.2,:"'you    ",:bugs bunny!
   hey2.3,:"'you    ",:    
   hey2.4,:"'you    ":
   :
	__EOT

   ${cat} <<- '__EOT' |\
      6< .tin2 ${MAILX} ${ARGS} -X 'readctl create 6' > "${MBOX}" 2>&1
   set ifs=:
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   unset a b c;read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   readctl remove 6;echo readctl remove:$?/$^ERRNAME
	__EOT
   check behave:read-ifs 0 "${MBOX}" '890153490 298'

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" 2>&1
   readctl create .tin
   readall d; echo $?/$^ERRNAME / <$d>
   wysh set d;readall d;echo $?/$^ERRNAME / <$d>
   readctl create .tin2
   readall d; echo $?/$^ERRNAME / <$d>
   wysh set d;readall d;echo $?/$^ERRNAME / <$d>
   readctl remove .tin;echo $?/$^ERRNAME;\
      readctl remove .tin2;echo $?/$^ERRNAME
	__EOT
   check behave:readall 0 "${MBOX}" '860434889 333'

   t_epilog
}

t_behave_mbox() {
   t_prolog t_behave_mbox
   TRAP_EXIT_ADDONS="./.t*"

   (
      i=0
      while [ ${i} -lt 112 ]; do
         printf 'm file://%s\n~s Subject %s\nHello %s!\n~.\n' \
            "${MBOX}" "${i}" "${i}"
         i=`add ${i} 1`
      done
   ) | ${MAILX} ${ARGS}
   check behave:mbox-1 0 "${MBOX}" '1140119864 13780'

   printf 'File "%s"\ncopy * "%s"\nFile "%s"\nfrom*' "${MBOX}" .tmbox1 .tmbox1 |
      ${MAILX} ${ARGS} > .tlst
   check behave:mbox-2 0 .tlst '2739893312 9103'

   printf 'File "%s"\ncopy * "file://%s"\nFile "file://%s"\nfrom*' \
      "${MBOX}" .tmbox2 .tmbox2 | ${MAILX} ${ARGS} > .tlst
   check behave:mbox-3 0 .tlst '1702194178 9110'

   # only the odd (even)
   (
      printf 'File "file://%s"\ncopy ' .tmbox2
      i=0
      while [ ${i} -lt 112 ]; do
         j=`modulo ${i} 2`
         [ ${j} -eq 1 ] && printf '%s ' "${i}"
         i=`add ${i} 1`
      done
      printf 'file://%s\nFile "file://%s"\nfrom*' .tmbox3 .tmbox3
   ) | ${MAILX} ${ARGS} > .tlst
   check behave:mbox-4 0 .tmbox3 '631132924 6890'
   check behave:mbox-5 - .tlst '2960975049 4573'
   # ...
   (
      printf 'file "file://%s"\nmove ' .tmbox2
      i=0
      while [ ${i} -lt 112 ]; do
         j=`modulo ${i} 2`
         [ ${j} -eq 0 ] && [ ${i} -ne 0 ] && printf '%s ' "${i}"
         i=`add ${i} 1`
      done
      printf 'file://%s\nFile "file://%s"\nfrom*\nFile "file://%s"\nfrom*' \
         .tmbox3 .tmbox3 .tmbox2
   ) | ${MAILX} ${ARGS} > .tlst
   check behave:mbox-6 0 .tmbox3 '1387070539 13655'
   ${sed} 2d < .tlst > .tlstx
   check behave:mbox-7 - .tlstx '2729940494 13645'

   t_epilog
}

t_behave_maildir() {
   t_prolog t_behave_maildir
   TRAP_EXIT_ADDONS="./.t*"

   (
      i=0
      while [ ${i} -lt 112 ]; do
         printf 'm file://%s\n~s Subject %s\nHello %s!\n~.\n' \
            "${MBOX}" "${i}" "${i}"
         i=`add ${i} 1`
      done
   ) | ${MAILX} ${ARGS}
   check behave:maildir-1 0 "${MBOX}" '1140119864 13780'

   printf 'File "%s"
         copy * "%s"
         File "%s"
         from*
      ' "${MBOX}" .tmdir1 .tmdir1 |
      ${MAILX} ${ARGS} -Snewfolders=maildir > .tlst
   check behave:maildir-2 0 .tlst '1797938753 9103'

   printf 'File "%s"
         copy * "maildir://%s"
         File "maildir://%s"
         from*
      ' "${MBOX}" .tmdir2 .tmdir2 |
      ${MAILX} ${ARGS} > .tlst
   check behave:maildir-3 0 .tlst '1155631089 9113'

   printf 'File "maildir://%s"
         copy * "file://%s"
         File "file://%s"
         from*
      ' .tmdir2 .tmbox1 .tmbox1 |
      ${MAILX} ${ARGS} > .tlst
   check behave:maildir-4 0 .tmbox1 '2646131190 13220'
   check behave:maildir-5 - .tlst '3701297796 9110'

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
   ) | ${MAILX} ${ARGS} > .tlst
   check behave:maildir-6 0 .tmbox2 '142890131 6610'
   check behave:maildir-7 - .tlst '960096773 4573'
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
   ) | ${MAILX} ${ARGS} > .tlst
   check behave:maildir-8 0 .tmbox2 '3806905791 13100'
   ${sed} 2d < .tlst > .tlstx
   check behave:maildir-9 - .tlstx '4216815295 13645'

   t_epilog
}

t_behave_record_a_resend() {
   t_prolog t_behave_record_a_resend
   TRAP_EXIT_ADDONS="./.t.record ./.t.resent"

   printf '
         set record=%s
         m %s\n~s Subject 1.\nHello.\n~.
         set record-files add-file-recipients
         m %s\n~s Subject 2.\nHello.\n~.
         File %s
         resend 2 ./.t.resent
         Resend 1 ./.t.resent
         set record-resent
         resend 2 ./.t.resent
         Resend 1 ./.t.resent
      ' ./.t.record "${MBOX}" "${MBOX}" "${MBOX}" |
      ${MAILX} ${ARGS}

   check behave:record_a_resend-1 0 "${MBOX}" '3057873538 256'
   check behave:record_a_resend-2 - .t.record '391356429 460'
   check behave:record_a_resend-3 - .t.resent '2685231691 648'

   t_epilog
}

t_behave_e_H_L_opts() {
   t_prolog t_behave_e_H_L_opts
   TRAP_EXIT_ADDONS="./.tsendmail.sh ./.t.mbox"

   touch ./.t.mbox
   ${MAILX} ${ARGS} -ef ./.t.mbox
   echo ${?} > "${MBOX}"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From Alchemilla Wed Apr 07 17:03:33 2017' && ${cat} && echo
			) >> "./.t.mbox"
	_EOT
   chmod 0755 ./.tsendmail.sh
   printf 'm me@exam.ple\nLine 1.\nHello.\n~.\n' |
   ${MAILX} ${ARGS} -Smta=./.tsendmail.sh
   printf 'm you@exam.ple\nLine 1.\nBye.\n~.\n' |
   ${MAILX} ${ARGS} -Smta=./.tsendmail.sh

   ${MAILX} ${ARGS} -ef ./.t.mbox
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -efL @t@me ./.t.mbox
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -efL @t@you ./.t.mbox
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -efL '@>@Line 1' ./.t.mbox
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -efL '@>@Hello.' ./.t.mbox
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -efL '@>@Bye.' ./.t.mbox
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -efL '@>@Good bye.' ./.t.mbox
   echo ${?} >> "${MBOX}"

   ${MAILX} ${ARGS} -fH ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -fL @t@me ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -fL @t@you ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -fL '@>@Line 1' ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -fL '@>@Hello.' ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -fL '@>@Bye.' ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   ${MAILX} ${ARGS} -fL '@>@Good bye.' ./.t.mbox >> "${MBOX}" 2>/dev/null
   echo ${?} >> "${MBOX}"

   check behave:e_H_L_opts - "${MBOX}" '1708955574 678'

   t_epilog
}

t_behave_alternates() {
   t_prolog t_behave_alternates
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From Valeriana Sat Jul 08 15:54:03 2017' && ${cat} && echo
			) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -Smta=./.tsendmail.sh > ./.tall 2>&1
   echo --0
   alternates
   echo $?/$^ERRNAME
   alternates a1@b1 a2@b2 a3@b3
   echo $?/$^ERRNAME
   alternates
   echo $?/$^ERRNAME
   vput alternates rv
   echo $?/$^ERRNAME <$rv>

   echo --1
   unalternates a2@b2
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   unalternates a3@b3
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   unalternates a1@b1
   vput alternates rv
   echo $?/$^ERRNAME <$rv>

   echo --2
   unalternates *
   alternates a1@b1 a2@b2 a3@b3
   unalternates a3@b3
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   unalternates a2@b2
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   unalternates a1@b1
   vput alternates rv
   echo $?/$^ERRNAME <$rv>

   echo --3
   alternates a1@b1 a2@b2 a3@b3
   unalternates a1@b1
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   unalternates a2@b2
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   unalternates a3@b3
   vput alternates rv
   echo $?/$^ERRNAME <$rv>

   echo --4
   unalternates *
   alternates a1@b1 a2@b2 a3@b3
   unalternates *
   vput alternates rv
   echo $?/$^ERRNAME <$rv>

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
   alternates a1@b1
   echo $?/$^ERRNAME
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   alternates a2@b2
   echo $?/$^ERRNAME
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   alternates a3@b3
   echo $?/$^ERRNAME
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   alternates a4@b4
   echo $?/$^ERRNAME
   vput alternates rv
   echo $?/$^ERRNAME <$rv>

   unalternates *
   vput alternates rv
   echo $?/$^ERRNAME <$rv>

   echo --11
   set posix
   alternates a1@b1 a2@b2
   echo $?/$^ERRNAME
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
   alternates a3@b3 a4@b4
   echo $?/$^ERRNAME
   vput alternates rv
   echo $?/$^ERRNAME <$rv>
	__EOT
   check behave:alternates-1 0 "${MBOX}" '142184864 515'
   check behave:alternates-2 - .tall '1878598364 505'

   t_epilog
}

t_behave_alias() {
   t_prolog t_behave_alias
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From Hippocastanum Mon Jun 19 15:07:07 2017' && ${cat} && echo
			) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} -Smta=./.tsendmail.sh > ./.tall 2>&1
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
   check behave:alias-1 0 "${MBOX}" '2496925843 272'
   check behave:alias-2 - .tall '3548953204 152'

   # TODO t_behave_alias: n_ALIAS_MAXEXP is compile-time constant,
   # TODO need to somehow provide its contents to the test, then test

   t_epilog
}

t_behave_filetype() {
   t_prolog t_behave_filetype
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From Alchemilla Wed Apr 25 15:12:13 2017' && ${cat} && echo
			) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   printf 'm m1@e.t\nL1\nHy1\n~.\nm m2@e.t\nL2\nHy2\n~@ %s\n~.\n' \
      "${SRCDIR}snailmail.jpg" | ${MAILX} ${ARGS} -Smta=./.tsendmail.sh
   check behave:filetype-1 0 "${MBOX}" '1594682963 13520'

   if (echo | gzip -c) >/dev/null 2>&1; then
      ${rm} -f ./.t.mbox*
      {
         printf 'File "%s"\ncopy 1 ./.t.mbox.gz\ncopy 2 ./.t.mbox.gz' \
            "${MBOX}" | ${MAILX} ${ARGS} \
               -X'filetype gz gzip\ -dc gzip\ -c'
         printf 'File ./.t.mbox.gz\ncopy * ./.t.mbox\n' |
            ${MAILX} ${ARGS} -X'filetype gz gzip\ -dc gzip\ -c'
      } > ./.t.out 2>&1
      check behave:filetype-2 - "./.t.mbox" '1594682963 13520'
      check behave:filetype-3 - "./.t.out" '2392348396 102'
   else
      echo 'behave:filetype-2: unsupported, skipped'
      echo 'behave:filetype-3: unsupported, skipped'
   fi

   {
      ${rm} -f ./.t.mbox*
      printf 'File "%s"\ncopy 1 ./.t.mbox.gz
            copy 2 ./.t.mbox.gz
            copy 1 ./.t.mbox.gz
            copy 2 ./.t.mbox.gz
            ' "${MBOX}" |
         ${MAILX} ${ARGS} \
            -X'filetype gz gzip\ -dc gzip\ -c' \
            -X'filetype mbox.gz "${sed} 1,3d|${cat}" \
            "echo eins;echo zwei;echo und mit ${sed} bist Du dabei;${cat}"'
      printf 'File ./.t.mbox.gz\ncopy * ./.t.mbox\n' |
         ${MAILX} ${ARGS} \
            -X'filetype gz gzip\ -dc gzip\ -c' \
            -X'filetype mbox.gz "${sed} 1,3d|${cat}" kill\ 0'
   } > ./.t.out 2>&1
   check behave:filetype-4 - "./.t.mbox" '2886541147 27060'
   check behave:filetype-5 - "./.t.out" '852335377 172'

   t_epilog
}

t_behave_message_injections() {
   t_prolog t_behave_message_injections
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From Echinacea Tue Jun 20 15:54:02 2017' && ${cat} && echo
			) > "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   echo mysig > ./.tmysig

   echo some-body | ${MAILX} ${ARGS} -Smta=./.tsendmail.sh \
      -Smessage-inject-head=head-inject \
      -Smessage-inject-tail=tail-inject \
      -Ssignature=./.tmysig \
      ex@am.ple > ./.tall 2>&1
   check behave:message_injections-1 0 "${MBOX}" '2434746382 134'
   check behave:message_injections-2 - .tall '4294967295 0' # empty file

   ${cat} <<-_EOT > ./.template
	From: me
	To: ex1@am.ple
	Cc: ex2@am.ple
	Subject: This subject is

   Body, body, body me.
	_EOT
   < ./.template ${MAILX} ${ARGS} -t -Smta=./.tsendmail.sh \
      -Smessage-inject-head=head-inject \
      -Smessage-inject-tail=tail-inject \
      -Ssignature=./.tmysig \
      > ./.tall 2>&1
   check behave:message_injections-3 0 "${MBOX}" '3114203412 198'
   check behave:message_injections-4 - .tall '4294967295 0' # empty file

   t_epilog
}

t_behave_compose_hooks() { # TODO monster
   t_prolog t_behave_compose_hooks
   TRAP_EXIT_ADDONS="./.t*"

   (echo line one&&echo line two&&echo line three) > ./.treadctl
   (echo echo four&&echo echo five&&echo echo six) > ./.tattach

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From PrimulaVeris Wed Apr 10 22:59:00 2017' && ${cat} && echo
         ) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   ${cat} <<'__EOT__' > ./.trc
   define bail {
      echoerr "Failed: $1.  Bailing out"; echo "~x"; xit
   }
   define xerr {
      vput vexpr es substr "$1" 0 1
      if [ "$es" != 2 ]
         xcall bail "$2"
      end
   }
   define read_mline_res {
      read hl; wysh set len=$? es=$! en=$^ERRNAME;\
         echo $len/$es/$^ERRNAME: $hl
      if [ $es -ne $^ERR-NONE ]
         xcall bail read_mline_res
      elif [ $len -ne 0 ]
         \xcall read_mline_res
      end
   }
   define ins_addr {
      wysh set xh=$1
      echo "~^header list"; read hl; echo $hl;\
         call xerr "$hl" "in_addr ($xh) 0-1"

      echo "~^header insert $xh diet <$xh@exam.ple> spliced";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 1-1"
      echo "~^header insert $xh <${xh}2@exam.ple>";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 1-2"
      echo "~^header insert $xh ${xh}3@exam.ple";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 1-3"
      echo "~^header list $xh"; read hl; echo $hl;\
         call xerr "$hl" "ins_addr $xh 1-4"
      echo "~^header show $xh"; read es; call xerr $es "ins_addr $xh 1-5"
      call read_mline_res

      if [ "$t_remove" == "" ]
         return
      end

      echo "~^header remove $xh"; read es; call xerr $es "ins_addr $xh 2-1"
      echo "~^header remove $xh"; read es; vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 2-2"
      end
      echo "~^header list $xh"; read es; vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 2-3"
      end
      echo "~^header show $xh"; read es; vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 2-4"
      end

      #
      echo "~^header insert $xh diet <$xh@exam.ple> spliced";\
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
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 3-9"
      end
      echo "~^header remove-at $xh T"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 505 ]
         xcall bail "ins_addr $xh 3-10"
      end
      echo "~^header list $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 3-11"
      end
      echo "~^header show $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 3-12"
      end

      #
      echo "~^header insert $xh diet <$xh@exam.ple> spliced";\
         read es; echo $es; call xerr "$es" "ins_addr $xh 4-1"
      echo "~^header insert $xh <${xh}2@exam.ple> (comment) \"Quot(e)d\"";\
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
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 4-9"
      end
      echo "~^header remove-at $xh T"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 505 ]
         xcall bail "ins_addr $xh 4-10"
      end
      echo "~^header list $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 4-11"
      end
      echo "~^header show $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_addr $xh 4-12"
      end
   }
   define ins_ref {
      wysh set xh=$1 mult=$2
      echo "~^header list"; read hl; echo $hl;\
         call xerr "$hl" "ins_ref ($xh) 0-1"

      echo "~^header insert $xh <$xh@exam.ple>";\
         read es; echo $es; call xerr "$es" "ins_ref $xh 1-1"
      if [ $mult -ne 0 ]
         echo "~^header insert $xh <${xh}2@exam.ple>";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 1-2"
         echo "~^header insert $xh ${xh}3@exam.ple";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 1-3"
      else
         echo "~^header insert $xh <${xh}2@exam.ple>"; read es;\
            vput vexpr es substr $es 0 3
         if [ $es != 506 ]
            xcall bail "ins_ref $xh 1-4"
         end
      end

      echo "~^header list $xh"; read hl; echo $hl;\
         call xerr "$hl" "ins_ref $xh 1-5"
      echo "~^header show $xh"; read es; call xerr $es "ins_ref $xh 1-6"
      call read_mline_res

      if [ "$t_remove" == "" ]
         return
      end

      echo "~^header remove $xh"; read es;\
         call xerr $es "ins_ref $xh 2-1"
      echo "~^header remove $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_ref $xh 2-2"
      end
      echo "~^header list $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "$es ins_ref $xh 2-3"
      end
      echo "~^header show $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_ref $xh 2-4"
      end

      #
      echo "~^header insert $xh <$xh@exam.ple>";\
         read es; echo $es; call xerr "$es" "ins_ref $xh 3-1"
      if [ $mult -ne 0 ]
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
      if [ $mult -ne 0 ] && [ $xh != subject ]
         echo "~^header remove-at $xh 1"; read es;\
            call xerr $es "ins_ref $xh 3-7"
         echo "~^header remove-at $xh 1"; read es;\
            call xerr $es "ins_ref $xh 3-8"
      end
      echo "~^header remove-at $xh 1"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_ref $xh 3-9"
      end
      echo "~^header remove-at $xh T"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 505 ]
         xcall bail "ins_ref $xh 3-10"
      end
      echo "~^header show $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_ref $xh 3-11"
      end

      #
      echo "~^header insert $xh <$xh@exam.ple> ";\
         read es; echo $es; call xerr "$es" "ins_ref $xh 4-1"
      if [ $mult -ne 0 ]
         echo "~^header insert $xh <${xh}2@exam.ple> ";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 4-2"
         echo "~^header insert $xh ${xh}3@exam.ple";\
            read es; echo $es; call xerr "$es" "ins_ref $xh 4-3"
      end
      echo "~^header list $xh"; read hl; echo $hl;\
         call xerr "$hl" "ins_ref $xh 4-4"
      echo "~^header show $xh"; read es; call xerr $es "ins_ref $xh 4-5"
      call read_mline_res

      if [ $mult -ne 0 ] && [ $xh != subject ]
         echo "~^header remove-at $xh 3"; read es;\
            call xerr $es "ins_ref $xh 4-6"
         echo "~^header remove-at $xh 2"; read es;\
            call xerr $es "ins_ref $xh 4-7"
      end
      echo "~^header remove-at $xh 1"; read es;\
         call xerr $es "ins_ref $xh 4-8"
      echo "~^header remove-at $xh 1"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_ref $xh 4-9"
      end
      echo "~^header remove-at $xh T"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 505 ]
         xcall bail "ins_ref $xh 4-10"
      end
      echo "~^header show $xh"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "ins_ref $xh 4-11"
      end
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
         read hl; echo $hl; vput vexpr es substr "$hl" 0 3
      if [ "$es" != 501 ]
         xcall bail "attach 0-1"
      end

      echo "~^attach attribute ./.treadctl";\
         read hl; echo $hl; vput vexpr es substr "$hl" 0 3
      if [ "$es" != 501 ]
         xcall bail "attach 0-2"
      end
      echo "~^attachment attribute-at 1";\
         read hl; echo $hl; vput vexpr es substr "$hl" 0 3
      if [ "$es" != 501 ]
         xcall bail "attach 0-3"
      end

      echo "~^attachment insert ./.treadctl=ascii";\
         read hl; echo $hl; call xerr "$hl" "attach 1-1"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 1-2"
      call read_mline_res
      echo "~^attachment attribute ./.treadctl";\
         read es; echo $es;call xerr "$es" "attach 1-3"
      call read_mline_res
      echo "~^attachment attribute .treadctl";\
         read es; echo $es;call xerr "$es" "attach 1-4"
      call read_mline_res
      echo "~^attachment attribute-at 1";\
         read es; echo $es;call xerr "$es" "attach 1-5"
      call read_mline_res

      echo "~^attachment attribute-set ./.treadctl filename rctl";\
         read es; echo $es;call xerr "$es" "attach 1-6"
      echo "~^attachment attribute-set .treadctl content-description Au";\
         read es; echo $es;call xerr "$es" "attach 1-7"
      echo "~^attachment attribute-set-at 1 content-id <10.du@ich>";\
         read es; echo $es;call xerr "$es" "attach 1-8"

      echo "~^attachment attribute ./.treadctl";\
         read es; echo $es;call xerr "$es" "attach 1-9"
      call read_mline_res
      echo "~^attachment attribute .treadctl";\
         read es; echo $es;call xerr "$es" "attach 1-10"
      call read_mline_res
      echo "~^attachment attribute rctl";\
         read es; echo $es;call xerr "$es" "attach 1-11"
      call read_mline_res
      echo "~^attachment attribute-at 1";\
         read es; echo $es;call xerr "$es" "attach 1-12"
      call read_mline_res

      #
      echo "~^attachment insert ./.tattach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 2-1"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 2-2"
      call read_mline_res
      echo "~^attachment attribute ./.tattach";\
         read es; echo $es;call xerr "$es" "attach 2-3"
      call read_mline_res
      echo "~^attachment attribute .tattach";\
         read es; echo $es;call xerr "$es" "attach 2-4"
      call read_mline_res
      echo "~^attachment attribute-at 2";\
         read es; echo $es;call xerr "$es" "attach 2-5"
      call read_mline_res

      echo "~^attachment attribute-set ./.tattach filename tat";\
         read es; echo $es;call xerr "$es" "attach 2-6"
      echo \
      "~^attachment attribute-set .tattach content-description Au2";\
         read es; echo $es;call xerr "$es" "attach 2-7"
      echo "~^attachment attribute-set-at 2 content-id <20.du@wir>";\
         read es; echo $es;call xerr "$es" "attach 2-8"
      echo \
         "~^attachment attribute-set-at 2 content-type application/x-sh";\
        read es; echo $es;call xerr "$es" "attach 2-9"

      echo "~^attachment attribute ./.tattach";\
         read es; echo $es;call xerr "$es" "attach 2-10"
      call read_mline_res
      echo "~^attachment attribute .tattach";\
         read es; echo $es;call xerr "$es" "attach 2-11"
      call read_mline_res
      echo "~^attachment attribute tat";\
         read es; echo $es;call xerr "$es" "attach 2-12"
      call read_mline_res
      echo "~^attachment attribute-at 2";\
         read es; echo $es;call xerr "$es" "attach 2-13"
      call read_mline_res

      #
      if [ "$t_remove" == "" ]
         return
      end

      echo "~^attachment remove ./.treadctl"; read es;\
         call xerr $es "attach 3-1"
      echo "~^attachment remove ./.tattach"; read es;\
         call xerr $es "attach 3-2"
      echo "~^   attachment     remove     ./.treadctl"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 3-3"
      end
      echo "~^   attachment     remove     ./.tattach"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 3-4"
      end
      echo "~^attachment list"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 3-5"
      end

      #
      echo "~^attachment insert ./.tattach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 4-1"
      echo "~^attachment insert ./.tattach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 4-2"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 4-3"
      call read_mline_res
      echo "~^   attachment     remove     .tattach"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 506 ]
         xcall bail "attach 4-4 $es"
      end
      echo "~^attachment remove-at T"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 505 ]
         xcall bail "attach 4-5"
      end
      echo "~^attachment remove ./.tattach"; read es;\
         call xerr $es "attach 4-6"
      echo "~^attachment remove ./.tattach"; read es;\
         call xerr $es "attach 4-7"
      echo "~^   attachment     remove     ./.tattach"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 4-8 $es"
      end
      echo "~^attachment list"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 4-9"
      end

      #
      echo "~^attachment insert ./.tattach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 5-1"
      echo "~^attachment insert ./.tattach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 5-2"
      echo "~^attachment insert ./.tattach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 5-3"
      echo "~^attachment list";\
         read es; echo $es;call xerr "$es" "attach 5-4"
      call read_mline_res

      echo "~^attachment remove-at 3"; read es;\
         call xerr $es "attach 5-5"
      echo "~^attachment remove-at 3"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 5-6"
      end
      echo "~^attachment remove-at 2"; read es;\
         call xerr $es "attach 5-7"
      echo "~^attachment remove-at 2"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 5-8"
      end
      echo "~^attachment remove-at 1"; read es;\
         call xerr $es "attach 5-9"
      echo "~^attachment remove-at 1"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 5-10"
      end

      echo "~^attachment list"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 5-11"
      end

      #
      echo "~^attachment insert ./.tattach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 6-1"
      echo "~^attachment insert ./.tattach=latin1";\
         read hl; echo $hl; call xerr "$hl" "attach 6-2"
      echo "~^attachment insert ./.tattach=latin1";\
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
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 6-8"
      end

      echo "~^attachment list"; read es;\
         vput vexpr es substr $es 0 3
      if [ $es != 501 ]
         xcall bail "attach 6-9"
      end

      echo t_attach LEAVE
   }
   define t_ocs {
      read ver
      echo t_ocs
      call t_header
      call t_attach
   }
   define t_oce {
      echo on-compose-enter, mailx-command<$mailx-command>
      alternates alter1@exam.ple alter2@exam.ple
      alternates
      set autocc='alter1@exam.ple alter2@exam.ple'
      echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
      echo mailx-subject<$mailx-subject>
      echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
      echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
         mailx-raw-bcc<$mailx-raw-bcc>
      echo mailx-orig-from<$mailx-orig-from> mailx-orig-to<$mailx-orig-to> \
         mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
   }
   define t_ocl {
      echo on-compose-leave, mailx-command<$mailx-command>
      vput alternates al
      eval alternates $al alter3@exam.ple alter4@exam.ple
      alternates
      set autobcc='alter3@exam.ple alter4@exam.ple'
      echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
      echo mailx-subject<$mailx-subject>
      echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
      echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
         mailx-raw-bcc<$mailx-raw-bcc>
      echo mailx-orig-from<$mailx-orig-from> mailx-orig-to<$mailx-orig-to> \
         mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
   }
   define t_occ {
      echo on-compose-cleanup, mailx-command<$mailx-command>
      unalternates *
      alternates
      echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
      echo mailx-subject<$mailx-subject>
      echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
      echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
         mailx-raw-bcc<$mailx-raw-bcc>
      echo mailx-orig-from<$mailx-orig-from> mailx-orig-to<$mailx-orig-to> \
         mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
   }
   wysh set on-compose-splice=t_ocs \
      on-compose-enter=t_oce on-compose-leave=t_ocl \
         on-compose-cleanup=t_occ
__EOT__

   ${rm} -f "${MBOX}"
   printf 'm this-goes@nowhere\nbody\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sstealthmua=noagent \
      -X'source ./.trc' -Smta=./.tsendmail.sh \
      >./.tall 2>&1
   ${cat} ./.tall >> "${MBOX}"
   check behave:compose_hooks-1 0 "${MBOX}" '522535560 10101'

   ${rm} -f "${MBOX}"
   printf 'm this-goes@nowhere\nbody\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sstealthmua=noagent \
      -St_remove=1 -X'source ./.trc' -Smta=./.tsendmail.sh \
      >./.tall 2>&1
   ${cat} ./.tall >> "${MBOX}"
   check behave:compose_hooks-2 0 "${MBOX}" '3654000499 12535'

   ##

   # Some state machine stress, shell compose hook, localopts for hook, etc.
   # readctl in child. ~r as HERE document
   ${rm} -f "${MBOX}"
   printf 'm ex@am.ple\nbody\n!.
      echon ${mailx-command}${mailx-subject}
      echon ${mailx-from}${mailx-sender}
      echon ${mailx-to}${mailx-cc}${mailx-bcc}
      echon ${mailx-raw-to}${mailx-raw-cc}${mailx-raw-bcc}
      echon ${mailx-orig-from}${mailx-orig-to}${mailx-orig-gcc}${mailx-orig-bcc}
      var t_oce t_ocs t_ocs_sh t_ocl t_occ autocc
   ' | ${MAILX} ${ARGS} -Snomemdebug -Sescape=! \
      -Smta=./.tsendmail.sh \
      -X'
         define bail {
            echoerr "Failed: $1.  Bailing out"; echo "~x"; xit
         }
         define xerr {
            vput vexpr es substr "$1" 0 1
            if [ "$es" != 2 ]
               xcall bail "$2"
            end
         }
         define read_mline_res {
            read hl; wysh set len=$? es=$! en=$^ERRNAME;\
               echo $len/$es/$^ERRNAME: $hl
            if [ $es -ne $^ERR-NONE ]
               xcall bail read_mline_res
            elif [ $len -ne 0 ]
               \xcall read_mline_res
            end
         }
         define _work {
            vput vexpr i + 1 "$2"
            if [ $i -lt 111 ]
               vput vexpr j % $i 10
               if [ $j -ne 0 ]
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
            wysh set line; read line;wysh set es=$? en=$^ERRNAME ;\
               echo read:$es/$en: $line
            if [ "${es}" -ne -1 ]
               xcall _read
            end
            readctl remove $cwd/.treadctl; echo readctl remove:$?/$^ERRNAME
         }
         define t_ocs {
            read ver
            echo t_ocs
            echo "~^header list"; read hl; echo $hl;\
               vput vexpr es substr "$hl" 0 1
            if [ "$es" != 2 ]
               xcall bail "header list"
            endif
            #
            call _work 1; echo $?
            echo "~^header insert cc splicy diet <splice@exam.ple> spliced";\
               read es; echo $es; vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be diet"
            endif
            echo "~^header insert cc <splice2@exam.ple>";\
               read es; echo $es; vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be diet2"
            endif
            #
            call _work 2; echo $?
            echo "~^header insert bcc juicy juice <juice@exam.ple> spliced";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be juicy"
            endif
            echo "~^header insert bcc juice2@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be juicy2"
            endif
            echo "~^header insert bcc juice3 <juice3@exam.ple>";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be juicy3"
            endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be juicy4"
            endif
            #
            echo "~^header remove-at bcc 3";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "remove juicy5"
            endif
            echo "~^header remove-at bcc 2";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "remove juicy6"
            endif
            echo "~^header remove-at bcc 3";\
               read es; echo $es;vput vexpr es substr "$es" 0 3
            if [ "$es" != 501 ]
               xcall bail "failed to remove-at"
            endif
            # Add duplicates which ought to be removed!
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be juicy4-1"
            endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be juicy4-2"
            endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               xcall bail "be juicy4-3"
            endif
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
            readctl create $cwd/.treadctl     ;echo readctl:$?/$^ERRNAME;\
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
            echo on-compose-enter, mailx-command<$mailx-command>
            set t_oce autobcc=oce@exam.ple
            alternates alter1@exam.ple alter2@exam.ple
            alternates
            echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
            echo mailx-subject<$mailx-subject>
            echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
            echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
               mailx-raw-bcc<$mailx-raw-bcc>
            echo mailx-orig-from<$mailx-orig-from> \
               mailx-orig-to<$mailx-orig-to> \
               mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
         }
         define t_ocl {
            echo on-compose-leave, mailx-command<$mailx-command>
            set t_ocl autocc=ocl@exam.ple
            unalternates *
            alternates alter3@exam.ple alter4@exam.ple
            alternates
            echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
            echo mailx-subject<$mailx-subject>
            echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
            echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
               mailx-raw-bcc<$mailx-raw-bcc>
            echo mailx-orig-from<$mailx-orig-from> \
               mailx-orig-to<$mailx-orig-to> \
               mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
         }
         define t_occ {
            echo on-compose-cleanup, mailx-command<$mailx-command>
            set t_occ autocc=occ@exam.ple
            unalternates *
            alternates
            echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
            echo mailx-subject<$mailx-subject>
            echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
            echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
               mailx-raw-bcc<$mailx-raw-bcc>
            echo mailx-orig-from<$mailx-orig-from> \
               mailx-orig-to<$mailx-orig-to> \
               mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
         }
         wysh set on-compose-splice=t_ocs \
            on-compose-splice-shell="read ver;printf \"t_ocs-shell\\n\
               ~t shell@exam.ple\\n~:set t_ocs_sh\\n\"" \
            on-compose-enter=t_oce on-compose-leave=t_ocl \
            on-compose-cleanup=t_occ
      ' > ./.tnotes 2>&1
   ex0_test behave:compose_hooks-3-estat
   ${cat} ./.tnotes >> "${MBOX}"

   check behave:compose_hooks-3 - "${MBOX}" '679526364 2431'

   # Reply, forward, resend, Resend

   ${rm} -f "${MBOX}"
   printf 'set from=f1@z\nm t1@z\nb1\n!.\nset from=f2@z\nm t2@z\nb2\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! \
      -Smta=./.tsendmail.sh

   printf '
      echo start: $? $! $^ERRNAME
      File %s
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
      resend 1 2 resendex@am.ple
      echo resend 1 2: $? $! $^ERRNAME;echo;echo
      Resend 1 2 Resendex@am.ple
      echo Resend 1 2: $? $! $^ERRNAME;echo;echo
   ' "${MBOX}" |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! \
      -Smta=./.tsendmail.sh \
      -X'
         define bail {
            echoerr "Failed: $1.  Bailing out"; echo "~x"; xit
         }
         define xerr {
            vput vexpr es substr "$1" 0 1
            if [ "$es" != 2 ]
               xcall bail "$2"
            end
         }
         define read_mline_res {
            read hl; wysh set len=$? es=$! en=$^ERRNAME;\
               echo mline_res:$len/$es/$^ERRNAME: $hl
            if [ $es -ne $^ERR-NONE ]
               xcall bail read_mline_res
            elif [ $len -ne 0 ]
               \xcall read_mline_res
            end
         }
         define work_hl {
            echo "~^header show $1"; read es;\
               call xerr $es "work_hl $1"; echo $1; call read_mline_res
            if [ $# -gt 1 ]
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
            echo on-XY-enter, mailx-command<$mailx-command>
            set t_oce autobcc=oce@exam.ple
            echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
            echo mailx-subject<$mailx-subject>
            echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
            echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
               mailx-raw-bcc<$mailx-raw-bcc>
            echo mailx-orig-from<$mailx-orig-from> \
               mailx-orig-to<$mailx-orig-to> \
               mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
         }
         define t_ocl {
            echo on-XY-leave, mailx-command<$mailx-command>
            set t_ocl autocc=ocl@exam.ple
            echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
            echo mailx-subject<$mailx-subject>
            echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
            echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
               mailx-raw-bcc<$mailx-raw-bcc>
            echo mailx-orig-from<$mailx-orig-from> \
               mailx-orig-to<$mailx-orig-to> \
               mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
         }
         define t_occ {
            echo on-XY-cleanup, mailx-command<$mailx-command>
            set t_occ autocc=occ@exam.ple
            echo mailx-from<$mailx-from> mailx-sender<$mailx-sender>
            echo mailx-subject<$mailx-subject>
            echo mailx-to<$mailx-to> mailx-cc<$mailx-cc> mailx-bcc<$mailx-bcc>
            echo mailx-raw-to<$mailx-raw-to> mailx-raw-cc<$mailx-raw-cc> \
               mailx-raw-bcc<$mailx-raw-bcc>
            echo mailx-orig-from<$mailx-orig-from> \
               mailx-orig-to<$mailx-orig-to> \
               mailx-orig-cc<$mailx-orig-cc> mailx-orig-bcc<$mailx-orig-bcc>
         }
         wysh set on-compose-splice=t_ocs \
            on-compose-enter=t_oce on-compose-leave=t_ocl \
               on-compose-cleanup=t_occ \
            on-resend-enter=t_oce on-resend-cleanup=t_occ
      ' > ./.tnotes 2>&1
   ex0_test behave:compose_hooks-4-estat
   ${cat} ./.tnotes >> "${MBOX}"

   check behave:compose_hooks-4 - "${MBOX}" '3038884027 7516'

   t_epilog
}

t_behave_mass_recipients() {
   t_prolog t_behave_mass_recipients
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From Eucalyptus Sat Jul 08 21:14:57 2017' && ${cat} && echo
			) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   ${cat} <<'__EOT__' > ./.trc
   define bail {
      echoerr "Failed: $1.  Bailing out"; echo "~x"; xit
   }
   define ins_addr {
      wysh set nr=$1 hn=$2
      echo "~$hn $hn$nr@$hn"; echo '~:echo $?'; read es
      if [ "$es" -ne 0 ]
        xcall bail "ins_addr $hn 1-$nr"
      end
      vput vexpr nr + $nr 1
      if [ "$nr" -le "$maximum" ]
         xcall ins_addr $nr $hn
      end
   }
   define bld_alter {
      wysh set nr=$1 hn=$2
      alternates $hn$nr@$hn
      vput vexpr nr + $nr 2
      if [ "$nr" -le "$maximum" ]
         xcall bld_alter $nr $hn
      end
   }
   define t_ocs {
      read ver
      call ins_addr 1 t
      call ins_addr 1 c
      call ins_addr 1 b
   }
   define t_ocl {
      if [ "$t_remove" != '' ]
         call bld_alter 1 t
         call bld_alter 2 c
      end
   }
   set on-compose-splice=t_ocs on-compose-leave=t_ocl
__EOT__

   ${rm} -f "${MBOX}"
   printf 'm this-goes@nowhere\nbody\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sstealthmua=noagent \
      -X'source ./.trc' -Smta=./.tsendmail.sh -Smaximum=2001 \
      >./.tall 2>&1
   ${cat} ./.tall >> "${MBOX}"
   check behave:mass_recipients-1 0 "${MBOX}" '2912243346 51526'

   ${rm} -f "${MBOX}"
   printf 'm this-goes@nowhere\nbody\n!.\n' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! -Sstealthmua=noagent \
      -St_remove=1 -X'source ./.trc' -Smta=./.tsendmail.sh -Smaximum=2001 \
      >./.tall 2>&1
   ${cat} ./.tall >> "${MBOX}"
   check behave:mass_recipients-2 0 "${MBOX}" '4097804632 34394'

   t_epilog
}

t_behave_mime_types_load_control() {
   t_prolog t_behave_mime_types_load_control
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tmts1
   @ application/mathml+xml mathml
	_EOT
   ${cat} <<-_EOT > ./.tmts2
   @ x-conference/x-cooltalk ice
   @ aga-aga aga
   @ application/aga-aga aga
	_EOT

   ${cat} <<-_EOT > ./.tmts1.mathml
   <head>nonsense ML</head>
	_EOT
   ${cat} <<-_EOT > ./.tmts2.ice
   Icy, icy road.
	_EOT
   printf 'of which the crack is coming soon' > ./.tmtsx.doom
   printf 'of which the crack is coming soon' > ./.tmtsx.aga

   printf '
         m %s
         Schub-di-du
~@ ./.tmts1.mathml
~@ ./.tmts2.ice
~@ ./.tmtsx.doom
~@ ./.tmtsx.aga
~.
         File %s
         from*
         type
         xit
      ' "${MBOX}" "${MBOX}" |
      ${MAILX} ${ARGS} \
         -Smimetypes-load-control=f=./.tmts1,f=./.tmts2 \
         > ./.tout 2>&1
   ex0_test behave:mime_types_load_control

   ${cat} "${MBOX}" >> ./.tout
   check behave:mime_types_load_control-1 - ./.tout '1441260727 2449'

   echo type | ${MAILX} ${ARGS} -R \
      -Smimetypes-load-control=f=./.tmts1,f=./.tmts3 \
      -f "${MBOX}" >> ./.tout 2>&1
   check behave:mime_types_load_control-2 0 ./.tout '1441391438 3646'

   t_epilog
}

t_behave_lreply_futh_rth_etc() {
   t_prolog t_behave_lreply_futh_rth_etc
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From HumulusLupulus Thu Jul 27 14:41:20 2017' && ${cat} && echo
			) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   ${cat} <<-_EOT > ./.tmbox
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

   #

   ${cat} <<-'_EOT' | ${MAILX} ${ARGS} -Sescape=! -Smta=./.tsendmail.sh \
         -Rf ./.tmbox >> "${MBOX}" 2>&1
	define r {
	   wysh set m="This is text of \"reply ${1}."
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
	   wysh set m="This is text of \"Reply ${1}."
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
	   wysh set m="This is text of \"Lreply ${1}." on-compose-splice=_Lh n=$2
	   eval Lreply $2
	}
	define L {
	   # We need two indirections for this test: one for the case that Lreply
	   # fails because of missing recipients: we need to read EOF next, thus
	   # place this in _Ls last; and second for the succeeding cases EOF is
	   # not what these should read, so go over the backside and splice it in!
	   call _Ls "$@"
	   echo -----After Lreply $1.$2: $?/$^ERRNAME
	}
	define x {
	   localopts call-fixate yes
	   call r $1
	   call R $1 1; call R $1 2; call R $1 3; call R $1 4
	   call L $1 1; call L $1 2; call L $1 3
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
	_EOT

   check behave:lreply_futh_rth_etc-1 0 "${MBOX}" '940818845 29373'

   ##

   ${cat} <<-_EOT > ./.tmbox
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

   printf 'reply 1\nthank you\n!.\n' |
      ${MAILX} ${ARGS} -Sescape=! -Smta=./.tsendmail.sh -Sreply-to-honour \
         -Rf ./.tmbox > "${MBOX}" 2>&1
   check behave:lreply_futh_rth_etc-2 0 "${MBOX}" '1045866991 331'

   t_epilog
}

t_behave_xxxheads_rfc2047() {
   t_prolog t_behave_xxxheads_rfc2047
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From GentianaLutea Mon Dec 04 17:15:29 2017' && ${cat} &&
         echo) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   #
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s 'a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲' \
      "${MBOX}"
   check behave:xxxheads_rfc2047-1 0 "${MBOX}" '3370931614 375'

   # Single word (overlong line split -- bad standard! Requires injection of
   # artificial data!!  But can be prevented by using RFC 2047 encoding)
   ${rm} -f "${MBOX}"
   i=`${awk} 'BEGIN{for(i=0; i<92; ++i) printf "0123456789_"}'`
   echo | ${MAILX} ${ARGS} -s "${i}" "${MBOX}"
   check behave:xxxheads_rfc2047-2 0 "${MBOX}" '489922370 1718'

   # Combination of encoded words, space and tabs of varying sort
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s "1Abrä Kaspas1 2Abra Katä	b_kaspas2  \
3Abrä Kaspas3   4Abrä Kaspas4    5Abrä Kaspas5     \
6Abra Kaspas6      7Abrä Kaspas7       8Abra Kaspas8        \
9Abra Kaspastäb4-3 	 	 	 10Abra Kaspas1 _ 11Abra Katäb1	\
12Abra Kadabrä1 After	Tab	after	Täb	this	is	NUTS" \
      "${MBOX}"
   check behave:xxxheads_rfc2047-3 0 "${MBOX}" '1676887734 591'

   # Overlong multibyte sequence that must be forcefully split
   # todo This works even before v15.0, but only by accident
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s "✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄" \
      "${MBOX}"
   check behave:xxxheads_rfc2047-4 0 "${MBOX}" '3029301775 659'

   # Trailing WS
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} \
      -s "1-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-5 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-6 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      "${MBOX}"
   check behave:xxxheads_rfc2047-5 0 "${MBOX}" '4126167195 297'

   # Leading and trailing WS
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} \
      -s "	 	 2-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      "${MBOX}"
   check behave:xxxheads_rfc2047-6 0 "${MBOX}" '3600624479 236'

   # RFC 2047 in an address field!  (Missing test caused v14.9.6!)
   ${rm} -f "${MBOX}"
   echo "Dat Früchtchen riecht häußlich" |
      ${MAILX} ${ARGS} ${ADDARG_UNI} -Sfullnames -Smta=./.tsendmail.sh \
         -s Hühöttchen \
         'Schnödes "Früchtchen" <do@du> (Hä!)'
   check behave:xxxheads_rfc2047-7 0 "${MBOX}" '800505986 368'

   t_epilog
}

t_behave_rfc2231() {
   t_prolog t_behave_rfc2231
   TRAP_EXIT_ADDONS="./.t*"

   (
      mkdir ./.ttt || exit 1
      cd ./.ttt || exit 2
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
      -a "./.ttt/ma'ger.txt" -a "./.ttt/mä'ger.txt" \
      -a './.ttt/diet\ is \curd.txt' -a './.ttt/diet "is" curd.txt' \
      -a ./.ttt/höde-tröge.txt \
      -a ./.ttt/höde__tröge__müde__dätte__hätte__vülle__gülle__äse__äße__säuerliche__kräuter__österliche__grüße__mäh.txt \
      -a ./.ttt/höde__tröge__müde__dätte__hätte__vuelle__guelle__aese__aesse__sauerliche__kräuter__österliche__grüße__mäh.txt \
      -a ./.ttt/hööööööööööööööööö_nöööööööööööööööööööööö_düüüüüüüüüüüüüüüüüüü_bäääääääääääääääääääääääh.txt \
      -a ./.ttt/✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆.txt \
      "${MBOX}"
   check behave:rfc2231-1 0 "${MBOX}" '684985954 3092'

   # `resend' test, reusing $MBOX
   printf "Resend ./.t2\nx\n" | ${MAILX} ${ARGS} -Rf "${MBOX}"
   check behave:rfc2231-2 0 ./.t2 '684985954 3092'

   printf "resend ./.t3\nx\n" | ${MAILX} ${ARGS} -Rf "${MBOX}"
   check behave:rfc2231-3 0 ./.t3 '3130352658 3148'

   t_epilog
}

t_behave_iconv_mbyte_base64() {
   t_prolog t_behave_iconv_mbyte_base64
   TRAP_EXIT_ADDONS="./.t*"

   if [ -n "${UTF8_LOCALE}" ] && have_feat iconv &&
         (</dev/null iconv -f ascii -t iso-2022-jp) >/dev/null 2>&1 ||
         (</dev/null iconv -f ascii -t euc-jp) >/dev/null 2>&1; then
      :
   else
      echo 'behave:iconv_mbyte_base64: unsupported, skipped'
      return
   fi

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From DroseriaRotundifolia Thu Aug 03 17:26:25 2017' && ${cat} &&
         echo) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   if (</dev/null iconv -f ascii -t iso-2022-jp) >/dev/null 2>&1; then
      cat <<-'_EOT' | LC_ALL=${UTF8_LOCALE} ${MAILX} ${ARGS} \
            -Smta=./.tsendmail.sh \
            -Sescape=! -Smime-encoding=base64 2>./.terr
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
      check behave:iconv_mbyte_base64-1 0 "${MBOX}" '3428985079 1976'
      check behave:iconv_mbyte_base64-2 - ./.terr '4294967295 0'

      printf 'eval f 1; write ./.twrite\n' |
         ${MAILX} ${ARGS} ${ADDARG_UNI} -Rf "${MBOX}" >./.tlog 2>&1
      check behave:iconv_mbyte_base64-3 0 ./.twrite '1259742080 686'
      check behave:iconv_mbyte_base64-4 - ./.tlog '3956097665 119'
   else
      echo 'behave:iconv_mbyte_base64: ISO-2022-JP unsupported, skipping 1-4'
   fi

   if (</dev/null iconv -f ascii -t euc-jp) >/dev/null 2>&1; then
      rm -f "${MBOX}" ./.twrite
      cat <<-'_EOT' | LC_ALL=${UTF8_LOCALE} ${MAILX} ${ARGS} \
            -Smta=./.tsendmail.sh \
            -Sescape=! -Smime-encoding=base64 2>./.terr
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
      check behave:iconv_mbyte_base64-5 0 "${MBOX}" '1686827547 2051'
      check behave:iconv_mbyte_base64-6 - ./.terr '4294967295 0'

      printf 'eval f 1; write ./.twrite\n' |
         ${MAILX} ${ARGS} ${ADDARG_UNI} -Rf "${MBOX}" >./.tlog 2>&1
      check behave:iconv_mbyte_base64-7 0 ./.twrite '1259742080 686'
      check behave:iconv_mbyte_base64-8 - ./.tlog '500059195 119'
   else
      echo 'behave:iconv_mbyte_base64: EUC-JP unsupported, skipping 5-8'
   fi

   t_epilog
}

t_behave_iconv_mainbody() {
   t_prolog t_behave_iconv_mainbody
   TRAP_EXIT_ADDONS="./.t*"

   # The different iconv(3) implementations use different replacement sequence
   # types (character-wise, byte-wise, and the character(s) used differ)
   i="${MAILX_ICONV_MODE}"
   if [ -z "${i}" ]; then
      echo 'behave:iconv_mainbody: unsupported, skipped'
      return
   fi

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From HamamelisVirginiana Fri Oct 20 16:23:21 2017' && ${cat} &&
			echo) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   printf '–' | ${MAILX} ${ARGS} ${ADDARG_UNI} -Smta=./.tsendmail.sh \
      -S charset-7bit=us-ascii -S charset-8bit=utf-8 \
      -s '–' over-the@rain.bow 2>./.terr
   check behave:iconv_mainbody-1 0 "${MBOX}" '3634015017 251'
   check behave:iconv_mainbody-2 - ./.terr '4294967295 0'

   printf '–' | ${MAILX} ${ARGS} ${ADDARG_UNI} -Smta=./.tsendmail.sh \
      -S charset-7bit=us-ascii -S charset-8bit=us-ascii \
      -s '–' over-the@rain.bow 2>./.terr
   exn0_test behave:iconv_mainbody-3
   check behave:iconv_mainbody-3 - "${MBOX}" '3634015017 251'
   check behave:iconv_mainbody-4 - ./.terr '2579894983 148'

   printf 'p\nx\n' | ${MAILX} ${ARGS} -Rf "${MBOX}" >./.tout 2>./.terr
   j=${?}
   ex0_test behave:iconv_mainbody-5-0 ${j}
   check behave:iconv_mainbody-5-1 - ./.terr '4294967295 0'
   if [ ${i} -eq 13 ]; then
      check behave:iconv_mainbody-5-2 - ./.tout '189327996 283'
   elif [ ${i} -eq 12 ]; then
      check behave:iconv_mainbody-5-3 - ./.tout '1959197095 283'
   elif [ ${i} -eq 3 ]; then
      check behave:iconv_mainbody-5-4 - ./.tout '3196380198 279'
   else
      check behave:iconv_mainbody-5-5 - ./.tout '3760313827 279'
   fi

   t_epilog
}

t_behave_q_t_etc_opts() {
   t_prolog t_behave_q_t_etc_opts
   TRAP_EXIT_ADDONS="./.t*"

   # Three tests for MIME encoding and (a bit) content classification.
   # At the same time testing -q FILE, < FILE and -t FILE
   t__put_body > ./.tin

   ${rm} -f "${MBOX}"
   < ./.tin ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -a ./.tin -s "`t__put_subject`" "${MBOX}"
   check behave:q_t_etc_opts-1 0 "${MBOX}" '3570973309 6646'

   ${rm} -f "${MBOX}"
   < /dev/null ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -a ./.tin -s "`t__put_subject`" -q ./.tin "${MBOX}"
   check behave:q_t_etc_opts-2 0 "${MBOX}" '3570973309 6646'

   ${rm} -f "${MBOX}"
   (  echo "To: ${MBOX}" && echo "Subject: `t__put_subject`" && echo &&
      ${cat} ./.tin
   ) | ${MAILX} ${ARGS} ${ADDARG_UNI} -Snodot -a ./.tin -t
   check behave:q_t_etc_opts-3 0 "${MBOX}" '3570973309 6646'

   t_epilog
}

t_behave_s_mime() {
   have_feat smime || {
      echo 'behave:s/mime: unsupported, skipped'
      return
   }

   t_prolog t_behave_s_mime
   TRAP_EXIT_ADDONS="./.t.conf ./.tkey.pem ./.tcert.pem ./.tpair.pem"
   TRAP_EXIT_ADDONS="${TRAP_EXIT_ADDONS} ./.VERIFY ./.DECRYPT ./.ENCRYPT"
   TRAP_EXIT_ADDONS="${TRAP_EXIT_ADDONS} ./.tsendmail.sh"

   printf 'behave:s/mime: .. generating test key and certificate ..\n'
   ${cat} <<-_EOT > ./.t.conf
		[ req ]
		default_bits           = 1024
		default_keyfile        = keyfile.pem
		distinguished_name     = req_distinguished_name
		attributes             = req_attributes
		prompt                 = no
		output_password        =

		[ req_distinguished_name ]
		C                      = GB
		ST                     = Over the
		L                      = rainbow
		O                      = S-nail
		OU                     = S-nail.smime
		CN                     = S-nail.test
		emailAddress           = test@localhost

		[ req_attributes ]
		challengePassword =
	_EOT
   openssl req -x509 -nodes -days 3650 -config ./.t.conf \
      -newkey rsa:1024 -keyout ./.tkey.pem -out ./.tcert.pem >/dev/null 2>&1
   ${cat} ./.tkey.pem ./.tcert.pem > ./.tpair.pem

   # Sign/verify
   printf 'behave:s/mime:sign/verify: '
   echo bla | ${MAILX} ${ARGS} \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -s 'S/MIME test' ./.VERIFY
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      printf 'failed\n'
      ESTAT=1
      t_epilog
      return
   fi

   ${awk} '
      BEGIN{ skip=0 }
      /^Content-Description: /{ skip = 2; print; next }
      /^$/{ if(skip) --skip }
      { if(!skip) print }
   ' \
      < ./.VERIFY > "${MBOX}"
   check behave:s/mime:sign/verify:checksum - "${MBOX}" '2900817158 648'

   printf 'behave:s/mime:sign/verify:verify '
   printf 'verify\nx\n' |
   ${MAILX} ${ARGS} \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -Serrexit -R \
      -f ./.VERIFY >/dev/null 2>&1
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      printf 'failed\n'
      ESTAT=1
      t_epilog
      return
   fi

   printf 'behave:s/mime:sign/verify:disproof-1 '
   if openssl smime -verify -CAfile ./.tcert.pem \
         -in ./.VERIFY >/dev/null 2>&1; then
      printf 'ok\n'
   else
      printf 'failed\n'
      ESTAT=1
      t_epilog
      return
   fi

   # (signing +) encryption / decryption
   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${SHELL} -
		(echo 'From Euphrasia Thu Apr 27 17:56:23 2017' && ${cat}) > ./.ENCRYPT
	_EOT
   chmod 0755 ./.tsendmail.sh

   printf 'behave:s/mime:encrypt+sign: '
   echo bla |
   ${MAILX} ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./.tpair.pem \
      -Smta=./.tsendmail.sh \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -s 'S/MIME test' recei@ver.com
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'error: encrypt+sign failed\n'
   fi

   ${sed} -e '/^$/,$d' < ./.ENCRYPT > "${MBOX}"
   check behave:s/mime:encrypt+sign:checksum - "${MBOX}" '1937410597 327'

   printf 'behave:s/mime:decrypt+verify: '
   printf 'decrypt ./.DECRYPT\nfi ./.DECRYPT\nverify\nx\n' |
   ${MAILX} ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./.tpair.pem \
      -Smta=./.tsendmail.sh \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -Serrexit -R \
      -f ./.ENCRYPT >/dev/null 2>&1
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'failed\n'
   fi

   ${awk} '
      BEGIN{ skip=0 }
      /^Content-Description: /{ skip = 2; print; next }
      /^$/{ if(skip) --skip }
      { if(!skip) print }
   ' \
      < ./.DECRYPT > "${MBOX}"
   check behave:s/mime:decrypt+verify:checksum - "${MBOX}" '1720739247 931'

   printf 'behave:s/mime:decrypt+verify:disproof-1: '
   if (openssl smime -decrypt -inkey ./.tkey.pem -in ./.ENCRYPT |
         openssl smime -verify -CAfile ./.tcert.pem) >/dev/null 2>&1; then
      printf 'ok\n'
   else
      printf 'failed\n'
      ESTAT=1
   fi

   printf "behave:s/mime:encrypt: "
   echo bla | ${MAILX} ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./.tpair.pem \
      -Smta=./.tsendmail.sh \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Sfrom=test@localhost \
      -s 'S/MIME test' recei@ver.com
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'failed\n'
   fi

   # Same as behave:s/mime:encrypt+sign:checksum above
   ${sed} -e '/^$/,$d' < ./.ENCRYPT > "${MBOX}"
   check behave:s/mime:encrypt:checksum - "${MBOX}" '1937410597 327'

   ${rm} -f ./.DECRYPT
   printf 'decrypt ./.DECRYPT\nx\n' | ${MAILX} ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./.tpair.pem \
      -Smta=./.tsendmail.sh \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Sfrom=test@localhost \
      -Serrexit -R \
      -f ./.ENCRYPT >/dev/null 2>&1
   check behave:s/mime:decrypt 0 "./.DECRYPT" '2624716890 422'

   printf 'behave:s/mime:decrypt:disproof-1: '
   if openssl smime -decrypt -inkey ./.tkey.pem \
         -in ./.ENCRYPT >/dev/null 2>&1; then
      printf 'ok\n'
   else
      printf 'failed\n'
      ESTAT=1
   fi

   t_epilog
}

# t_content()
# Some basic tests regarding correct sending of mails, via STDIN / -t / -q,
# including basic MIME Content-Transfer-Encoding correctness (quoted-printable)
# Note we unfortunately need to place some statements without proper
# indentation because of continuation problems
# xxx Note: t_content() was the first test (series) written.  Today many
# xxx aspects are (better) covered by other tests above, some are not.
# xxx At some future date and time, convert the last remains not covered
# xxx elsewhere to a real t_behave_* test and drop t_content()
t_content() {
   t_prolog t_content
 
   # Test for [260e19d] (Juergen Daubert)
   ${rm} -f "${MBOX}"
   echo body | ${MAILX} ${ARGS} "${MBOX}"
   check content:004 0 "${MBOX}" '2917662811 98'

   # "Test for" [d6f316a] (Gavin Troy)
   ${rm} -f "${MBOX}"
   printf "m ${MBOX}\n~s subject1\nEmail body\n~.\nfi ${MBOX}\np\nx\n" |
   ${MAILX} ${ARGS} ${ADDARG_UNI} -Spipe-text/plain="@* ${cat}" > "${BODY}"
   check content:006 0 "${MBOX}" '2099098650 122'
   check content:006-1 - "${BODY}" '794542938 174'

   # "Test for" [c299c45] (Peter Hofmann) TODO shouldn't end up QP-encoded?
   ${rm} -f "${MBOX}"
   ${awk} 'BEGIN{
      for(i = 0; i < 10000; ++i)
         printf "\xC3\xBC"
         #printf "\xF0\x90\x87\x90"
      }' | ${MAILX} ${ARGS} ${ADDARG_UNI} -s TestSubject "${MBOX}"
   check content:007 0 "${MBOX}" '534262374 61816'

   t_epilog
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

t_all() {
#   if have_feat devel; then
#      ARGS="${ARGS} -Smemdebug"
#      export ARGS
#   fi

   if [ -n "${UTF8_LOCALE}" ]; then
      printf 'Using Unicode locale %s\n' "${UTF8_LOCALE}"
   else
      printf 'No Unicode locale found, disabling Unicode tests\n'
   fi

   t_behave
   t_content
}

if [ -z "${CHECK_ONLY}${MAE_TEST}" ]; then
   cc_all_configs
elif [ -z "${MAE_TEST}" ] || [ ${#} -eq 0 ]; then
   t_all
else
   while [ ${#} -gt 0 ]; do
      ${1}
      shift
   done
fi

[ ${ESTAT} -eq 0 ] && echo Ok || echo >&2 'Errors occurred'

exit ${ESTAT}
# s-sh-mode
