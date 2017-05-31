#!/bin/sh -
#@ Usage: ./cc-test.sh [--check-only] s-mailx-binary
#@ TODO _All_ the tests should happen in a temporary subdir.
# Public Domain

# We need *stealthmua* regardless of $SOURCE_DATE_EPOCH, the program name as
# such is a compile-time variable
ARGS='-:/ -# -Sdotlock-ignore-error -Sencoding=quoted-printable -Sstealthmua'
   ARGS="${ARGS}"' -Snosave -Sexpandaddr=restrict'
CONF=./make.rc
BODY=./.cc-body.txt
MBOX=./.cc-test.mbox
MAIL=/dev/null
#UTF8_LOCALE= autodetected unless set

#MEMTESTER='valgrind --log-file=.vl-%p '
MEMTESTER=

if ( command -v command ) >/dev/null 2>&1; then :; else
   command() {
      shift
      which "${@}"
   }
fi

MAKE="${MAKE:-`command -v make`}"
awk=${awk:-`command -v awk`}
cat=${cat:-`command -v cat`}
cksum=${cksum:-`command -v cksum`}
rm=${rm:-`command -v rm`}
sed=${sed:-`command -v sed`}
grep=${grep:-`command -v grep`}

# Problem: force $SHELL to be a real shell.  It seems some testing environments
# use nologin(?), but we need a real shell for command execution
if { echo ${SHELL} | ${grep} nologin; } >/dev/null 2>&1; then
   echo >&2 '$SHELL seems to be nologin, overwriting to /bin/sh!'
   SHELL=/bin/sh
   export SHELL
fi

# We sometimes "fake" sendmail(1) a.k.a. *mta* with a shell wrapper, and it
# happens that /bin/sh is often terribly slow
if command -v dash >/dev/null 2>&1; then
   MYSHELL="`command -v dash`"
elif command -v mksh >/dev/null 2>&1; then
   MYSHELL="`command -v mksh`"
else
   MYSHELL="${SHELL}"
fi

##  --  >8  --  8<  --  ##

export ARGS CONF BODY MBOX MAIL  MAKE awk cat cksum rm sed grep

LC_ALL=C LANG=C ADDARG_UNI=-Sttycharset=UTF-8
TZ=UTC
# Wed Oct  2 01:50:07 UTC 1996
SOURCE_DATE_EPOCH=844221007

export LC_ALL LANG ADDARG_UNI TZ SOURCE_DATE_EPOCH
unset POSIXLY_CORRECT

if [ -z "${UTF8_LOCALE}" ]; then
   UTF8_LOCALE=
   if command -v locale >/dev/null 2>&1; then
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

usage() {
   echo >&2 "Usage: ./cc-test.sh [--check-only] s-mailx-binary"
   exit 1
}

CHECK_ONLY= MAILX=
while [ $# -gt 0 ]; do
   if [ "${1}" = --check-only ]; then
      CHECK_ONLY=1
   else
      MAILX=${1}
   fi
   shift
done
[ -x "${MAILX}" ] || usage
RAWMAILX=${MAILX}
MAILX="${MEMTESTER}${MAILX}"
export RAWMAILX MAILX

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
         printf "CONFIG=MEDIUM OPT_AUTOCC=0\n"
         printf "CONFIG=MEDIUM OPT_AUTOCC=1\n"
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
      sh -c "${MAKE} ${c}"
      t_all
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
}
t_epilog() {
   t_prolog
}

check() {
   restat=${?} tid=${1} eestat=${2} f=${3} s=${4}
       #x=`echo ${tid} | tr "/:=" "__-"`
       #cp -f "${f}" "${TMPDIR}/${x}"
   [ "${eestat}" != - ] && [ "${restat}" != "${eestat}" ] &&
      err "${tid}" 'unexpected exit status: '"${restat} != ${eestat}"
   csum="`${cksum} < ${f}`"
   if [ "${csum}" = "${s}" ]; then
      printf '%s: ok\n' "${tid}"
   else
      ESTAT=1
      printf '%s: error: checksum mismatch (got %s)\n' "${tid}" "${csum}"
   fi
}

err() {
   ESTAT=1
   printf '%s: error: %s\n' ${1} "${2}"
}

ex0_test() {
   [ $? -ne 0 ] && err $1 'unexpected non-0 exit status'
}

exn0_test() {
   [ $? -eq 0 ] && err $1 'unexpected 0 exit status'
}

if ( [ "$((1 + 1))" = 2 ] ) >/dev/null 2>&1; then
   add() {
      echo "$((${1} + ${2}))"
   }
elif command -v expr >/dev/null 2>&1; then
   add() {
      echo `expr ${1} + ${2}`
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
elif command -v expr >/dev/null 2>&1; then
   modulo() {
      echo `expr ${1} % ${2}`
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
   t_behave_wysh
   t_behave_input_inject_semicolon_seq
   t_behave_commandalias
   t_behave_ifelse
   t_behave_localopts
   t_behave_macro_param_shift
   t_behave_addrcodec
   t_behave_vexpr
   t_behave_call_ret
   t_behave_xcall
   t_behave_vpospar

   t_behave_mbox

   # FIXME t_behave_alias
   # FIXME t_behave_mlist
   t_behave_filetype

   t_behave_record_a_resend

   t_behave_e_H_L_opts
   t_behave_compose_hooks
   t_behave_mime_types_load_control

   t_behave_smime

   t_behave_maildir
}

t_behave_X_opt_input_command_stack() {
   t_prolog

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
   t_prolog

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

t_behave_wysh() {
   t_prolog

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

   t_epilog
}

t_behave_input_inject_semicolon_seq() {
   t_prolog

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
   t_prolog

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
   t_prolog

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
		if $dietcurd != 'yoho'
		   echo 6.err
		else
		   echo 6.ok
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
		# integer conversion, <..>..
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
		   if $dietcurd > abB
		      echo 12.ok2
		   else
		      echo 12.err2
		   endif
		   if $dietcurd == aBC
		      echo 12.ok3
		   else
		      echo 12.err3
		   endif
		   if $dietcurd >= AbC
		      echo 12.ok4
		   else
		      echo 12.err4
		   endif
		   if $dietcurd <= ABc
		      echo 12.ok5
		   else
		      echo 12.err5
		   endif
		   if $dietcurd >= abd
		      echo 12.err6
		   else
		      echo 12.ok6
		   endif
		   if $dietcurd <= abb
		      echo 12.err7
		   else
		      echo 12.ok7
		   endif
		else
		   echo 12.err1
		endif
		if $dietcurd =@ aB
		   echo 13.ok
		else
		   echo 13.err
		endif
		if $dietcurd =@ bC
		   echo 14.ok
		else
		   echo 14.err
		endif
		if $dietcurd !@ aB
		   echo 15.err
		else
		   echo 15.ok
		endif
		if $dietcurd !@ bC
		   echo 15.err
		else
		   echo 15.ok
		endif
		if $dietcurd =@ Cd
		   echo 16.err
		else
		   echo 16.ok
		endif
		if $dietcurd !@ Cd
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

   check behave:if-normal 0 "${MBOX}" '557629289 631'

   if have_feat regex; then
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}"
			set dietcurd=yoho
			if $dietcurd =~ '^yo.*'
			   echo 1.ok
			else
			   echo 1.err
			endif
			if $dietcurd =~ '^yoho.+'
			   echo 2.err
			else
			   echo 2.ok
			endif
			if $dietcurd !~ '.*ho$'
			   echo 3.err
			else
			   echo 3.ok
			endif
			if $dietcurd !~ '.+yoho$'
			   echo 4.ok
			else
			   echo 4.err
			endif
			if [ $dietcurd !~ '.+yoho$' ]
			   echo 5.ok
			else
			   echo 5.err
			endif
			if ! [ $dietcurd =~ '.+yoho$' ]
			   echo 6.ok
			else
			   echo 6.err
			endif
			if ! ! [ $dietcurd !~ '.+yoho$' ]
			   echo 7.ok
			else
			   echo 7.err
			endif
			if ! [ ! [ $dietcurd !~ '.+yoho$' ] ]
			   echo 8.ok
			else
			   echo 8.err
			endif
			if [ ! [ ! [ $dietcurd !~ '.+yoho$' ] ] ]
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

      check behave:if-regex 0 "${MBOX}" '439960016 81'
   else
      printf 'behave:if-regex: unsupported, skipped\n'
   fi

   t_epilog
}

t_behave_localopts() {
   t_prolog

   # Nestable conditions test
   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}"
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
	__EOT

   check behave:localopts 0 "${MBOX}" '1936527193 192'

   t_epilog
}

t_behave_macro_param_shift() {
   t_prolog

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
   t_prolog

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}"
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
	__EOT

   check behave:addrcodec 0 "${MBOX}" '429099645 2414'

   t_epilog
}

t_behave_vexpr() {
   t_prolog

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

   ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" #2>/dev/null
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
	__EOT

   check behave:vexpr-string 0 "${MBOX}" '265398700 267'

   if have_feat regex; then
      ${cat} <<- '__EOT' | ${MAILX} ${ARGS} > "${MBOX}" #2>/dev/null
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
   t_prolog

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
   t_prolog

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

   check behave:xcall-1 0 "${MBOX}" '728629184 19115'

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
   t_prolog

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
   echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   vput vpospar x quote; echo x<$x>
   vpospar clear;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   eval vpospar set ${x};echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>

   wysh set ifs=$',\t'
   echo ifs<$ifs> ifs-ws<$ifs-ws>
   vpospar set hey, "'you    ", world!
   echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   vput vpospar x quote; echo x<$x>
   vpospar clear;echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
   eval vpospar set ${x};echo $?/$^ERRNAME/$#: $* / "$@" / <$1><$2><$3><$4>
	__EOT
   check behave:vpospar-ifs 0 "${MBOX}" '2015927702 706'

   t_epilog
}

t_behave_read() {
   t_prolog
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
   read a b c
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
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   read a b c
   echo $?/$^ERRNAME / <$a><$b><$c>
   readctl remove 6;echo readctl remove:$?/$^ERRNAME
	__EOT
   check behave:read-ifs 0 "${MBOX}" '890153490 298'

   t_epilog
}

t_behave_mbox() {
   t_prolog
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

   printf 'File "%s"
         copy * "%s"
         File "%s"
         from*
      ' "${MBOX}" .tmbox1 .tmbox1 |
      ${MAILX} ${ARGS} > .tlst
   check behave:mbox-2 0 .tlst '2739893312 9103'

   printf 'File "%s"
         copy * "file://%s"
         File "file://%s"
         from*
      ' "${MBOX}" .tmbox2 .tmbox2 |
      ${MAILX} ${ARGS} > .tlst
   check behave:mbox-3 0 .tlst '1702194178 9110'

   # only the odd (even)
   (
      printf 'File "file://%s"
            copy ' .tmbox2
      i=0
      while [ ${i} -lt 112 ]; do
         j=`modulo ${i} 2`
         [ ${j} -eq 1 ] && printf '%s ' "${i}"
         i=`add ${i} 1`
      done
      printf ' file://%s
            File "file://%s"
            from*
         ' .tmbox3 .tmbox3
   ) | ${MAILX} ${ARGS} > .tlst
   check behave:mbox-4 0 .tmbox3 '631132924 6890'
   check behave:mbox-5 - .tlst '2960975049 4573'
   # ...
   (
      printf 'file "file://%s"
            move ' .tmbox2
      i=0
      while [ ${i} -lt 112 ]; do
         j=`modulo ${i} 2`
         [ ${j} -eq 0 ] && [ ${i} -ne 0 ] && printf '%s ' "${i}"
         i=`add ${i} 1`
      done
      printf ' file://%s
            File "file://%s"
            from*
            File "file://%s"
            from*
         ' .tmbox3 .tmbox3 .tmbox2
   ) | ${MAILX} ${ARGS} > .tlst
   check behave:mbox-6 0 .tmbox3 '1387070539 13655'
   ${sed} 2d < .tlst > .tlstx
   check behave:mbox-7 - .tlstx '2729940494 13645'

   t_epilog
}

t_behave_filetype() {
   t_prolog
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${MYSHELL} -
		(echo 'From Alchemilla Wed Apr 25 15:12:13 2017' && ${cat} && echo
			) >> "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   printf 'm m1@e.t\nL1\nHy1\n~.\nm m2@e.t\nL2\nHy2\n~@ ./snailmail.jpg\n~.\n' |
      ${MAILX} ${ARGS} -Smta=./.tsendmail.sh
   check behave:filetype-1 0 "${MBOX}" '1645747150 13536'

   if command -v gzip >/dev/null 2>&1; then
      ${rm} -f ./.t.mbox*
      {
         printf 'File "%s"\ncopy 1 ./.t.mbox.gz
               copy 2 ./.t.mbox.gz' "${MBOX}" |
            ${MAILX} ${ARGS} \
               -X'filetype gz gzip\ -dc gzip\ -c'
         printf 'File ./.t.mbox.gz\ncopy * ./.t.mbox\n' |
            ${MAILX} ${ARGS} \
               -X'filetype gz gzip\ -dc gzip\ -c'
      } >/dev/null 2>&1
      check behave:filetype-2 0 "./.t.mbox" '1645747150 13536'
   else
      echo 'behave:filetype-2: unsupported, skipped'
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
   } >/dev/null 2>&1

   check behave:filetype-3 - "./.t.mbox" '238021003 27092'

   t_epilog
}

t_behave_record_a_resend() {
   t_prolog
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
   t_prolog
   TRAP_EXIT_ADDONS="./.tsendmail.sh ./.t.mbox"

   touch ./.t.mbox
   ${MAILX} ${ARGS} -ef ./.t.mbox
   echo ${?} > "${MBOX}"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${MYSHELL} -
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

t_behave_compose_hooks() {
   t_prolog
   TRAP_EXIT_ADDONS="./.t*"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!${MYSHELL} -
		${rm} -f "${MBOX}"
		(echo 'From PrimulaVeris Wed Apr 10 22:59:00 2017' && ${cat}) > "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   (echo line one&&echo line two&&echo line three) > ./.treadctl

   printf 'm hook-test@exam.ple\nbody\n!.\nvar t_oce t_ocs t_ocs_shell t_ocl' |
   ${MAILX} ${ARGS} -Snomemdebug -Sescape=! \
      -Smta=./.tsendmail.sh \
      -X'
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
            read line;wysh set es=$? en=$^ERRNAME ; echo read:$es/$en: $line
            if [ "${es}" -ne -1 ]
               xcall _read
            end
         }
         define t_ocs {
            read ver
            echo t_ocs
            echo "~^header list"; read hl; echo $hl;\
               vput vexpr es substr "$hl" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to header list, aborting send"; echo "~x"
            endif
            #
            call _work 1; echo $?
            echo "~^header insert cc splicy diet <splice@exam.ple> spliced";\
               read es; echo $es; vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be diet, aborting send"; echo "~x"
            endif
            echo "~^header insert cc <splice2@exam.ple>";\
               read es; echo $es; vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be diet2, aborting send"; echo "~x"
            endif
            #
            call _work 2; echo $?
            echo "~^header insert bcc juicy juice <juice@exam.ple> spliced";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be juicy, aborting send"; echo "~x"
            endif
            echo "~^header insert bcc juice2@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be juicy2, aborting send"; echo "~x"
            endif
            echo "~^header insert bcc juice3 <juice3@exam.ple>";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be juicy3, aborting send"; echo "~x"
            endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be juicy4, aborting send"; echo "~x"
            endif
            #
            echo "~^header remove-at bcc 3";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to remove juicy3, aborting send"; echo "~x"
            endif
            echo "~^header remove-at bcc 2";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to remove juicy2, aborting send"; echo "~x"
            endif
            echo "~^header remove-at bcc 3";\
               read es; echo $es;vput vexpr es substr "$es" 0 3
            if [ "$es" != 501 ]
               echoerr "Failed to failed to remove-at, aborting send"; echo "~x"
            endif
            # Add duplicates which ought to be removed!
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be juicy4-1, aborting send"; echo "~x"
            endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be juicy4-2, aborting send"; echo "~x"
            endif
            echo "~^header insert bcc juice4@exam.ple";\
               read es; echo $es;vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be juicy4-3, aborting send"; echo "~x"
            endif
            echo "~:set t_ocs"
            #
            call _work 3; echo $?
            echo "~r - __EOT"
            vput ! i echo just knock if you can hear me;\
               i=0;\
               while [ $i -lt 24 ]; do printf "%s " $i; i=`expr $i + 1`; done;\
               echo relax
            echon shell-cmd says $?/$^ERRNAME: $i
            echo "~x  will not become interpreted, we are reading until __EOT"
            echo "__EOT"
            #
            call _work 4; echo $?
            vput cwd cwd;echo cwd:$?
            readctl create $cwd/.treadctl     ;echo readctl:$?/$^ERRNAME
            xcall _read
            #
            call _work 5; echo $?
         }
         define t_oce {
            set t_oce autobcc=oce@exam.ple
         }
         define t_ocl {
            set t_ocl autocc=ocl@exam.ple
         }
         wysh set on-compose-splice=t_ocs \
            on-compose-splice-shell="read ver;printf \"t_ocs-shell\\n\
               ~t shell@exam.ple\\n~:set t_ocs_shell\\n\"" \
            on-compose-enter=t_oce on-compose-leave=t_ocl
      ' > ./.tnotes 2>&1
   ex0_test behave:compose_hooks
   ${cat} ./.tnotes >> "${MBOX}"

   check behave:compose_hooks - "${MBOX}" '1851329576 1049'

   t_epilog
}

t_behave_mime_types_load_control() {
   t_prolog
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
   check behave:mime_types_load_control-1 - ./.tout '529577037 2474'

   echo type | ${MAILX} ${ARGS} -R \
      -Smimetypes-load-control=f=./.tmts1,f=./.tmts3 \
      -f "${MBOX}" >> ./.tout 2>&1
   check behave:mime_types_load_control-2 0 ./.tout '2025926659 3558'

   t_epilog
}

t_behave_smime() {
   have_feat smime || {
      echo 'behave:s/mime: unsupported, skipped'
      return
   }

   t_prolog
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
		#!${MYSHELL} -
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

t_behave_maildir() {
   t_prolog
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

# t_content()
# Some basic tests regarding correct sending of mails, via STDIN / -t / -q,
# including basic MIME Content-Transfer-Encoding correctness (quoted-printable)
# Note we unfortunately need to place some statements without proper
# indentation because of continuation problems
t_content() {
   t_prolog

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
' '\
 > "${BODY}"

   # MIME encoding (QP) stress message subject
SUB="Äbrä  Kä?dä=brö 	 Fü?di=bus? \
adadaddsssssssddddddddddddddddddddd\
ddddddddddddddddddddddddddddddddddd\
ddddddddddddddddddddddddddddddddddd\
dddddddddddddddddddd Hallelulja? Od\
er?? eeeeeeeeeeeeeeeeeeeeeeeeeeeeee\
eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\
eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee f\
fffffffffffffffffffffffffffffffffff\
fffffffffffffffffffff ggggggggggggg\
ggggggggggggggggggggggggggggggggggg\
ggggggggggggggggggggggggggggggggggg\
ggggggggggggggggggggggggggggggggggg\
gggggggggggggggg"

   # Three tests for MIME encodign and (a bit) content classification.
   # At the same time testing -q FILE, < FILE and -t FILE

   ${rm} -f "${MBOX}"
   < "${BODY}" ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -a "${BODY}" -s "${SUB}" "${MBOX}"
   check content:001 0 "${MBOX}" '1145066634 6654'

   ${rm} -f "${MBOX}"
   < /dev/null ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -a "${BODY}" -s "${SUB}" -q "${BODY}" "${MBOX}"
   check content:002 0 "${MBOX}" '1145066634 6654'

   ${rm} -f "${MBOX}"
   (  echo "To: ${MBOX}" && echo "Subject: ${SUB}" && echo &&
      ${cat} "${BODY}"
   ) | ${MAILX} ${ARGS} ${ADDARG_UNI} -Snodot -a "${BODY}" -t
   check content:003 0 "${MBOX}" '1145066634 6654'

   # Test for [260e19d] (Juergen Daubert)
   ${rm} -f "${MBOX}"
   echo body | ${MAILX} ${ARGS} "${MBOX}"
   check content:004 0 "${MBOX}" '2917662811 98'

   # Sending of multiple mails in a single invocation
   ${rm} -f "${MBOX}"
   (  printf "m ${MBOX}\n~s subject1\nE-Mail Körper 1\n~.\n" &&
      printf "m ${MBOX}\n~s subject2\nEmail body 2\n~.\n" &&
      echo x
   ) | ${MAILX} ${ARGS} ${ADDARG_UNI}
   check content:005 0 "${MBOX}" '2098659767 358'

   ## $BODY CHANGED

   # "Test for" [d6f316a] (Gavin Troy)
   ${rm} -f "${MBOX}"
   printf "m ${MBOX}\n~s subject1\nEmail body\n~.\nfi ${MBOX}\np\nx\n" |
   ${MAILX} ${ARGS} ${ADDARG_UNI} -Spipe-text/plain="${cat}" > "${BODY}"
   check content:006 0 "${MBOX}" '2099098650 122'

   # "Test for" [c299c45] (Peter Hofmann) TODO shouldn't end up QP-encoded?
   ${rm} -f "${MBOX}"
   ${awk} 'BEGIN{
      for(i = 0; i < 10000; ++i)
         printf "\xC3\xBC"
         #printf "\xF0\x90\x87\x90"
      }' | ${MAILX} ${ARGS} ${ADDARG_UNI} -s TestSubject "${MBOX}"
   check content:007 0 "${MBOX}" '534262374 61816'

   ## Test some more corner cases for header bodies (as good as we can today) ##

   #
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s 'a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲' \
      "${MBOX}"
   check content:008 0 "${MBOX}" '3370931614 375'

   # Single word (overlong line split -- bad standard! Requires injection of
   # artificial data!!  Bad can be prevented by using RFC 2047 encoding)
   ${rm} -f "${MBOX}"
   i=`${awk} 'BEGIN{for(i=0; i<92; ++i) printf "0123456789_"}'`
   echo | ${MAILX} ${ARGS} -s "${i}" "${MBOX}"
   check content:009 0 "${MBOX}" '489922370 1718'

   # Combination of encoded words, space and tabs of varying sort
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s "1Abrä Kaspas1 2Abra Katä	b_kaspas2  \
3Abrä Kaspas3   4Abrä Kaspas4    5Abrä Kaspas5     \
6Abra Kaspas6      7Abrä Kaspas7       8Abra Kaspas8        \
9Abra Kaspastäb4-3 	 	 	 10Abra Kaspas1 _ 11Abra Katäb1	\
12Abra Kadabrä1 After	Tab	after	Täb	this	is	NUTS" \
      "${MBOX}"
   check content:010 0 "${MBOX}" '1676887734 591'

   # Overlong multibyte sequence that must be forcefully split
   # todo This works even before v15.0, but only by accident
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} ${ADDARG_UNI} \
      -s "✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄" \
      "${MBOX}"
   check content:011 0 "${MBOX}" '3029301775 659'

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
   check content:012 0 "${MBOX}" '4126167195 297'

   # Leading and trailing WS
   ${rm} -f "${MBOX}"
   echo | ${MAILX} ${ARGS} \
      -s "	 	 2-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      "${MBOX}"
   check content:013 0 "${MBOX}" '3600624479 236'

   # Quick'n dirty RFC 2231 test; i had more when implementing it, but until we
   # have a (better) test framework materialize a quick shot
   ${rm} -f "${MBOX}"
   TRAP_EXIT_ADDONS=./.ttt
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
   check content:014-1 0 "${MBOX}" '684985954 3092'

   # `resend' test, reusing $MBOX
   printf "Resend ${BODY}\nx\n" | ${MAILX} ${ARGS} -f "${MBOX}"
   check content:014-2 0 "${MBOX}" '684985954 3092'

   t_epilog
}

t_all() {
#   if have_feat devel; then
#      ARGS="${ARGS} -Smemdebug"
#      export ARGS
#   fi
   t_behave
   t_content
}

if [ -z "${CHECK_ONLY}" ]; then
   cc_all_configs
else
   t_all
fi

[ ${ESTAT} -eq 0 ] && echo Ok || echo >&2 'Errors occurred'

exit ${ESTAT}
# s-sh-mode
