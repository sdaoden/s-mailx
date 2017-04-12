#!/bin/mksh -
#@ Usage: ./cc-test.sh [--check-only [s-nail-binary]]
# Public Domain

ARGS='-:/ -# -Sdotlock-ignore-error -Sencoding=quoted-printable -Sstealthmua'
   ARGS="${ARGS}"' -Snosave -Sexpandaddr=restrict'
   ARGS="${ARGS}"' -Slog-prefix=classico:'
CONF=./make.rc
BODY=./.cc-body.txt
MBOX=./.cc-test.mbox
MAIL=/dev/null
#UTF8_LOCALE= autodetected unless set

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

##  --  >8  --  8<  --  ##

export SNAIL ARGS CONF BODY MBOX MAIL  MAKE awk cat cksum rm sed grep

LC_ALL=C LANG=C ADDARG_UNI=-Sttycharset=UTF-8
TZ=UTC
# Wed Oct  2 01:50:07 UTC 1996
SOURCE_DATE_EPOCH=844221007

export LC_ALL LANG ADDARG_UNI TZ SOURCE_DATE_EPOCH
unset POSIXLY_CORRECT

# Problem: force $SHELL to be a real shell.  It seems some testing environments
# use nologin(?), but we need a real shell for command execution
if { echo ${SHELL} | ${grep} nologin; } >/dev/null 2>&1; then
   echo >&2 '$SHELL seems to be nologin, overwriting to /bin/sh!'
   SHELL=/bin/sh
   export SHELL
fi

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
   echo >&2 "Usage: ./cc-test.sh [--check-only [s-nail-binary]]"
   exit 1
}

CHECK_ONLY=
[ ${#} -gt 0 ] && {
   [ "${1}" = --check-only ] || usage
   [ ${#} -gt 2 ] && usage
   [ ${#} -eq 2 ] && SNAIL="${2}"
   [ -x "${SNAIL}" ] || usage
   CHECK_ONLY=1
}

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

# cksum_test()
# Read mailbox $2, strip non-constant headers and MIME boundaries, query the
# cksum(1) of the resulting data and compare against the checksum $3
cksum_test() {
   tid=${1} f=${2} s=${3}
   printf "${tid}: "
   csum="`${sed} -e '/^From /d' \
         -e '/^ boundary=/d' -e '/^--=-=/d' < \"${f}\" \
         -e '/^\[-- Message/d' | ${cksum}`";
   if [ "${csum}" = "${s}" ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'error: checksum mismatch (got %s)\n' "${csum}"
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

have_feat() {
   ( "${SNAIL}" ${ARGS} -X'echo $features' -Xx | ${grep} +${1} ) >/dev/null 2>&1
}

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

   # FIXME t_behave_alias

   # FIXME t_behave_mlist

   have_feat smime && t_behave_smime

   t_behave_e_H_L_opts
   t_behave_compose_hooks
}

t_behave_X_opt_input_command_stack() {
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
   < "${BODY}" "${SNAIL}" ${ARGS} \
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
   ' > "${MBOX}" 2>/dev/null
#1
#mac0-1 via2 
#2
#1-1
#1-2
#mac1-1 via2 
#mac0-1 via2 mac1
#mac1-2
#mac2-1 via2 mac1
#mac0-1 via2 mac2
#mac2-2
#mac1-3
#1-3
#1-1-1 via2
#mac0-1 via2 
#1-1-2
#1-4
#3
#mac2-1 via2 
#mac0-1 via2 mac2
#mac2-2
#4
#1
#mac0-1 via1 
#2
#1-1
#1-2
#mac1-1 via1 
#mac0-1 via1 mac1
#mac1-2
#mac2-1 via1 mac1
#mac0-1 via1 mac2
#mac2-2
#mac1-3
#1-3
#1-1-1 via1
#mac0-1 via1 
#1-1-2
#1-4
#3
#mac2-1 via1 
#mac0-1 via1 mac2
#mac2-2
#4
   ex0_test behave:x_opt_input_command_stack
   cksum_test behave:x_opt_input_command_stack "${MBOX}" '1391275936 378'
}

t_behave_X_errexit() {
   ${cat} <<- '__EOT' > "${BODY}"
	echo one
	echos nono
	echo two
	__EOT

   </dev/null "${SNAIL}" ${ARGS} -Snomemdebug \
         -X'echo one' -X' echos nono ' -X'echo two' \
      > "${MBOX}" 2>&1
#one
#classico:Unknown command: `echos'
#two
   ex0_test behave:x_errexit-1
   cksum_test behave:x_errexit-1 "${MBOX}" '2893507350 42'

   </dev/null "${SNAIL}" ${ARGS} -X'source '"${BODY}" -Snomemdebug \
      > "${MBOX}" 2>&1
   ex0_test behave:x_errexit-2
   cksum_test behave:x_errexit-2 "${MBOX}" '2893507350 42'

   </dev/null MAILRC="${BODY}" "${SNAIL}" ${ARGS} -:u -Snomemdebug \
      > "${MBOX}" 2>&1
   ex0_test behave:x_errexit-3
   cksum_test behave:x_errexit-3 "${MBOX}" '2893507350 42'

   ##

   </dev/null "${SNAIL}" ${ARGS} -Serrexit -Snomemdebug \
         -X'echo one' -X' echos nono ' -X'echo two' \
      > "${MBOX}" 2>&1
#one
#classico:Unknown command: `echos'
   exn0_test behave:x_errexit-4
   cksum_test behave:x_errexit-4 "${MBOX}" '3287572983 38'

   </dev/null "${SNAIL}" ${ARGS} -X'source '"${BODY}" -Serrexit -Snomemdebug \
      > "${MBOX}" 2>&1
   exn0_test behave:x_errexit-5
   cksum_test behave:x_errexit-5 "${MBOX}" '3287572983 38'

   </dev/null MAILRC="${BODY}" "${SNAIL}" ${ARGS} -:u -Serrexit -Snomemdebug \
      > "${MBOX}" 2>&1
#one
#classico:Unknown command: `echos'
#classico:Alert: Stopped loading initialization resource ./.cc-body.txt due to errors (enable *debug* for trace)
   exn0_test behave:x_errexit-6
   cksum_test behave:x_errexit-6 "${MBOX}" '2718843754 150'

   </dev/null MAILRC="${BODY}" "${SNAIL}" ${ARGS} -:u -Sposix -Snomemdebug \
      > "${MBOX}" 2>&1
   exn0_test behave:x_errexit-7
   cksum_test behave:x_errexit-7 "${MBOX}" '2718843754 150'

   ## Repeat 4-7 with ignerr set

   ${sed} -e 's/^echos /ignerr echos /' < "${BODY}" > "${MBOX}"

   </dev/null "${SNAIL}" ${ARGS} -Serrexit -Snomemdebug \
         -X'echo one' -X'ignerr echos nono ' -X'echo two' \
      > "${BODY}" 2>&1
   ex0_test behave:x_errexit-8
   cksum_test behave:x_errexit-8 "${BODY}" '2893507350 42'

   </dev/null "${SNAIL}" ${ARGS} -X'source '"${MBOX}" -Serrexit -Snomemdebug \
      > "${BODY}" 2>&1
   ex0_test behave:x_errexit-9
   cksum_test behave:x_errexit-9 "${BODY}" '2893507350 42'

   </dev/null MAILRC="${MBOX}" "${SNAIL}" ${ARGS} -:u -Serrexit -Snomemdebug \
      > "${BODY}" 2>&1
   ex0_test behave:x_errexit-10
   cksum_test behave:x_errexit-10 "${BODY}" '2893507350 42'

   </dev/null MAILRC="${MBOX}" "${SNAIL}" ${ARGS} -:u -Sposix -Snomemdebug \
      > "${BODY}" 2>&1
   ex0_test behave:x_errexit-11
   cksum_test behave:x_errexit-11 "${BODY}" '2893507350 42'
}

t_behave_wysh() {
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
      LC_ALL=${UTF8_LOCALE} "${SNAIL}" ${ARGS} 2>/dev/null > "${MBOX}"
#abcd
#abcd
#abcd
#abcd
#abcd
#abcd
#abcd
#a b c d
#a b c d
#a b c d
#a b c d
#a$`"\
#a$`'"\
#a$`'"\
#a$`'"\
#a${DIET}b${TIED}c\${DIET}d\${TIED}e
#aCURDbc${DIET}d${TIED}e
#a${DIET}b${TIED}cCURDde
#aAAAAAAAc
#aAAÃAAc
#aÿc
#aÿc
#abd
#abde
#abdf
#abdg
#abdh
#abdi
#abdj
#abdk
#abdl
#abdm
#abdn
#abdo
#abdp
#abdq
#abdr
#abds
#abdt
#abdu
#a	b
#a	b
#a	b
#a	b
#a	b
#a
      ex0_test behave:wysh_unicode
      cksum_test behave:wysh_unicode "${MBOX}" '475805847 317'
   fi

   < "${BODY}" DIET=CURD TIED= "${SNAIL}" ${ARGS} > "${MBOX}" 2>/dev/null
#abcd
#abcd
#abcd
#abcd
#abcd
#abcd
#abcd
#a b c d
#a b c d
#a b c d
#a b c d
#a$`"\
#a$`'"\
#a$`'"\
#a$`'"\
#a${DIET}b${TIED}c\${DIET}d\${TIED}e
#aCURDbc${DIET}d${TIED}e
#a${DIET}b${TIED}cCURDde
#aAAAAAAAc
#aAA\u0C1AAc
#aÿc
#aÿc
#abd
#abde
#abdf
#abdg
#abdh
#abdi
#abdj
#abdk
#abdl
#abdm
#abdn
#abdo
#abdp
#abdq
#abdr
#abds
#abdt
#abdu
#a	b
#a	b
#a	b
#a	b
#a	b
#a
   ex0_test behave:wysh_c
   cksum_test behave:wysh_c "${MBOX}" '1473887148 321'
}

t_behave_input_inject_semicolon_seq() {
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}"
	define mydeepmac {
		echon '(mydeepmac)';
	}
	define mymac {
		echon this_is_mymac;call mydeepmac;echon ';';
	}
	echon one';';~mymac;echon two";";call mymac;echo three$';';
	define mymac {
		echon this_is_mymac;call mydeepmac;echon ,TOO'!;';
	}
	echon one';';~mymac;echon two";";call mymac;echo three$';';
	__EOT
#one;this_is_mymac(mydeepmac);two;this_is_mymac(mydeepmac);three;
#one;this_is_mymac(mydeepmac),TOO!;two;this_is_mymac(mydeepmac),TOO!;three;
   ex0_test behave:input_inject_semicolon_seq
   cksum_test behave:input_inject_semicolon_seq "${MBOX}" '512117110 140'
}

t_behave_commandalias() {
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}"
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
#hoho stop.
#hoho huhu
#huhu
#huhu
   ex0_test behave:commandalias
   cksum_test behave:commandalias "${MBOX}" '3694143612 31'
}

t_behave_ifelse() {
   # Nestable conditions test
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}"
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
   ex0_test behave:if-normal
   cksum_test behave:if-normal "${MBOX}" '557629289 631'

   if have_feat regex; then
      ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}"
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
      ex0_test behave:if-regex
      cksum_test behave:if-regex "${MBOX}" '439960016 81'
   fi
}

t_behave_localopts() {
   # Nestable conditions test
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}"
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
#in: t0
#in: t1
#in: t2
#t2
#gv1 lv1 lv2 lv3 gv2, t2
#gv1 gv2,
#gv1    gv2, 
#in: trouble
#in: t0
#in: t1
#in: t2
#t2
#gv1 lv1 lv2 lv3 gv2, t2
#gv1 gv2,
#gv1    gv2, 
#active trouble: gv1 gv2,
#active null: ,
   ex0_test behave:localopts
   cksum_test behave:localopts "${MBOX}" '1936527193 192'
}

t_behave_macro_param_shift() {
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}" 2>/dev/null
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
#in: t1
#in: t2
#t2.0 has 5/5 parameters: 1,you,get (1 you get four args) [1 you get four args]
#t2.1 has 4/4 parameters: you,get,four (you get four args) [you get four args]
#t2.2:0 has 2/2 parameters: four,args, (four args) [four args]
#t2.3:0 has 2/2 parameters: four,args, (four args) [four args]
#t2.4:0 has 1/1 parameters: args,, (args) [args]
#t1.1: 0; ignerr () should not exist
#in: t2
#t2.0 has 4/4 parameters: 1,you,get (1 you get three args) [1 you get three args]
#t2.1 has 3/3 parameters: you,get,three args (you get three args) [you get three args]
#t2.2:0 has 1/1 parameters: three args,, (three args) [three args]
#t2.3:0 has 1/1 parameters: three args,, (three args) [three args]
#t2.4:0 has 0/0 parameters: ,, () []
#t1.2: 0; ignerr () should not exist
#in: t2
#t2.0 has 3/3 parameters: 1,you,get two args (1 you get two args) [1 you get two args]
#t2.1 has 2/2 parameters: you,get two args, (you get two args) [you get two args]
#t2.2:0 has 0/0 parameters: ,, () []
#t2.3:0 has 0/0 parameters: ,, () []
#t2.4:0 has 0/0 parameters: ,, () []
#t1.3: 0; ignerr () should not exist
#in: t2
#t2.0 has 2/2 parameters: 1,you get one arg, (1 you get one arg) [1 you get one arg]
#t2.1 has 1/1 parameters: you get one arg,, (you get one arg) [you get one arg]
#t2.2:0 has 1/1 parameters: you get one arg,, (you get one arg) [you get one arg]
#t2.3:0 has 1/1 parameters: you get one arg,, (you get one arg) [you get one arg]
#t2.4:0 has 0/0 parameters: ,, () []
#t1.4: 0; ignerr () should not exist
#in: t2
#t2.0 has 2/2 parameters: ,you get one arg, ( you get one arg) [ you get one arg]
#t2.1 has 1/1 parameters: you get one arg,, (you get one arg) [you get one arg]
#t1.5: 1; ignerr () should not exist
   ex0_test behave:macro_param_shift
   cksum_test behave:macro_param_shift "${MBOX}" '1402489146 1682'
}

t_behave_addrcodec() {
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}"
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
	vput addrcodec res +++e 22 Hey\,\"  <doog@def> "Wie()" findet \" Dr. \" das?
	echo $?/$^ERRNAME $res
	eval vput addrcodec res d $res
	echo $?/$^ERRNAME $res
	#
	vput addrcodec res s \
		"23 Hey\\,\\\" \"Wie" () "\" findet \\\" Dr. \\\" das?" <doog@def>
	echo $?/$^ERRNAME $res
	__EOT
#0/NONE 1 <doog@def>
#0/NONE 1 <doog@def>
#0/NONE "2 ." <doog@def>
#0/NONE 2 . <doog@def>
#0/NONE "3 Sauer Dr." <doog@def>
#0/NONE 3 Sauer Dr. <doog@def>
#0/NONE 3.50 "Sauer \(Ma\) Dr." <doog@def>
#0/NONE 3.50 Sauer (Ma) Dr. <doog@def>
#0/NONE 3.51 "Sauer \(Ma\) \"Dr.\"" <doog@def>
#0/NONE 3.51 Sauer (Ma) "Dr." <doog@def>
#0/NONE 4 Sauer (Ma) "Dr." <doog@def>
#0/NONE 4 Sauer (Ma) Dr. <doog@def>
#0/NONE 5 Sauer (Ma) "Braten Dr." <doog@def>
#0/NONE 5 Sauer (Ma) Braten Dr. <doog@def>
#0/NONE 6 Sauer (Ma) "Braten Dr." (Heu) <doog@def>
#0/NONE 6 Sauer (Ma) Braten Dr. (Heu) <doog@def>
#0/NONE 7 Sauer (Ma) "Braten Dr." (Heu bu) <doog@def>
#0/NONE 7 Sauer (Ma) Braten Dr. (Heu bu) <doog@def>
#0/NONE "8 Dr. Sauer" (Ma) "Braten Dr." (Heu bu) "Boom. Boom" <doog@def>
#0/NONE 8 Dr. Sauer (Ma) Braten Dr. (Heu bu) Boom. Boom <doog@def>
#0/NONE "9 Dr.Sauer" (Ma) "Braten Dr." (Heu) <doog@def>
#0/NONE 9 Dr.Sauer (Ma) Braten Dr. (Heu) <doog@def>
#0/NONE 10 (Ma) "Braten Dr." (Heu) <doog@def>
#0/NONE 10 (Ma) Braten Dr. (Heu) <doog@def>
#0/NONE 11 (Ma) "Braten Dr\".\"" (Heu) <doog@def>
#0/NONE 11 (Ma) Braten Dr"." (Heu) <doog@def>
#0/NONE "12 Dr. Sauer" (Ma) "Braten Dr." (u) <doog@def>
#0/NONE 12 Dr. Sauer (Ma) Braten Dr. (u) <doog@def>
#0/NONE 13 (Ma) "Braten Dr." (Heu) <doog@def>
#0/NONE 13 (Ma) Braten Dr. (Heu) <doog@def>
#0/NONE "14 Hey, Du Wie" () "findet Dr. das?" () <doog@def>
#0/NONE 14 Hey, Du Wie () findet Dr. das? () <doog@def>
#0/NONE "15 Hey, Du Wie" () "findet \"\" Dr. \"\" das?" () <doog@def>
#0/NONE 15 Hey, Du Wie () findet "" Dr. "" das? () <doog@def>
#0/NONE "16 \"Hey,\" \"Du\" \"Wie" () "\" findet \"\" Dr. \"\" das?" () <doog@def>
#0/NONE 16 "Hey," "Du" "Wie () " findet "" Dr. "" das? () <doog@def>
#0/NONE "17 \"Hey\" Du \"Wie" () "findet \" \" Dr. \"\"\" das?" () <doog@def>
#0/NONE 17 "Hey" Du "Wie () findet " " Dr. """ das? () <doog@def>
#0/NONE "18 \"Hey\" Du \"Wie" () "findet \" \" Dr. \"\"\" das?" () <doog@def>
#0/NONE 18 "Hey" Du "Wie () findet " " Dr. """ das? () <doog@def>
#0/NONE "19 Hey\\,\\\" \"Wie" () "\" findet \\\" Dr. \\\" das?" <doog@def>
#0/NONE 19 Hey\,\" "Wie () " findet \" Dr. \" das? <doog@def>
#1/INVAL 20 Hey\\,\\"  <doog@def> "Wie()" findet \\" Dr. \\" das?
#0/NONE "21 Hey\\,\\ Wie() findet \\  Dr. \\ das?" <doog@def>
#0/NONE 21 Hey\,\ Wie() findet \  Dr. \ das? <doog@def>
#0/NONE "22 Hey\,\" Wie() findet \" Dr. \" das?" <doog@def>
#0/NONE 22 Hey," Wie() findet " Dr. " das? <doog@def>
#0/NONE doog@def
   ex0_test behave:addrcodec
   cksum_test behave:addrcodec "${MBOX}" '3907388894 2416'
}

t_behave_vexpr() {
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}" 2>/dev/null
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
#0/NONE 9223372036854775807
#1/RANGE -1
#0/OVERFLOW 9223372036854775807
#0/NONE -9223372036854775808
#1/RANGE -1
#0/OVERFLOW -9223372036854775808
# #1
#0/NONE -1
#0/NONE -2
#0/NONE 0
# #2
#0/NONE 0
#0/NONE 1
#0/NONE 2
# #3
#0/NONE 9223372036854775807
#1/OVERFLOW -1
#0/OVERFLOW 9223372036854775807
#0/NONE 9223372036854775807
#1/OVERFLOW -1
#0/OVERFLOW 9223372036854775807
# #4
#0/NONE -9223372036854775808
#1/OVERFLOW -1
#0/OVERFLOW -9223372036854775808
#0/NONE -9223372036854775808
#1/OVERFLOW -1
#0/OVERFLOW -9223372036854775808
# #5
#0/NONE 0
#0/NONE -1
#0/NONE 0
# #6
#0/NONE 9223372036854775807
#1/OVERFLOW -1
#0/OVERFLOW 9223372036854775807
#0/NONE -9223372036854775807
#0/NONE -9223372036854775808
#1/OVERFLOW -1
#0/OVERFLOW -9223372036854775808
# #7
#0/NONE -9223372036854775808
#1/OVERFLOW -1
#0/OVERFLOW -9223372036854775808
#0/NONE -9223372036854775808
#1/OVERFLOW -1
#0/OVERFLOW -9223372036854775808
# #8
#0/NONE -15
#0/NONE 0
#0/NONE -1
#0/NONE 0
#0/NONE -11
# #9
#0/NONE 0
#0/NONE 0
#0/NONE 1
#0/NONE 26
# #10
#1/RANGE -1
#0/NONE 0
#0/NONE 1
#0/NONE 6
# #11
#1/RANGE -1
#0/NONE 0
#0/NONE 0
#0/NONE -1
   ex0_test behave:vexpr-numeric
   cksum_test behave:vexpr-numeric "${MBOX}" '1723609217 1048'

   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}" #2>/dev/null
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
#0/NONE :2:
#0/NONE :0:
#1/NODATA ::
#0/NONE :6:
# #1
#0/NONE :2:
#0/NONE :0:
#0/NONE :0:
#0/NONE :6:
# #2
#0/NONE :ananarama:
#0/NONE :anarama:
#0/NONE :arama:
#0/NONE :ama:
#0/NONE :a:
#0/NONE ::
#0/NONE :ana:
#0/NONE :ana:
#0/NONE :ara:
#0/NONE :ama:
#0/OVERFLOW :a:
#0/OVERFLOW ::
# #3
   ex0_test behave:vexpr-string
   cksum_test behave:vexpr-string "${MBOX}" '265398700 267'

   if have_feat regex; then
      ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} > "${MBOX}" #2>/dev/null
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
#0/NONE :2:
#0/NONE :0:
#1/NODATA ::
#0/NONE :6:
# #1
#0/NONE :2:
#0/NONE :0:
#0/NONE :0:
#0/NONE :6:
# #2
#0/NONE :baabananaramau{rama}:
#0/NONE :abananaramaunarama:
#1/NODATA ::
#0/NONE :bananabananarama:
# #3
#0/NONE :baabananaramau{rama}:
#0/NONE :abananaramaunarama:
#0/NONE :naramabananarama:
#0/NONE :bananabananarama:
# #4
      ex0_test behave:vexpr-regex
      cksum_test behave:vexpr-regex "${MBOX}" '3270360157 311'
   fi
}

t_behave_call_ret() {
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} -Snomemdebug > "${MBOX}" 2>&1
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
			wysh set i=$? j=$!
			echon "<$1/$i/$j "
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
			echon "<$1=$i/$j "
			return $i $j
		else
			vput vexpr j + $^ERR-BUSY $2
			echo ! The end for $1=$i/$j
			return $i $j
		end
		echoerr au
	}

	call w1 0; echo ?=$? !=$!; echo -----;
	call w2 0; echo ?=$? !=$!; echo -----;
	call w3 0 1; echo ?=$? !=$!; echo -----;
	__EOT
   ex0_test behave:call_ret
   cksum_test behave:call_ret "${MBOX}" '2240086482 5844'
}

t_behave_xcall() {
   ${cat} <<- '__EOT' | "${SNAIL}" ${ARGS} -Snomemdebug > "${MBOX}" 2>&1
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
	echo ?=$? !=$!
	#
	call work 0 yes
	echo ?=$? !=$!
	call xwork 0 yes
	echo ?=$? !=$!
	__EOT
   ex0_test behave:xcall
   cksum_test behave:xcall "${MBOX}" '1579767783 19097'
}

t_behave_e_H_L_opts() {
   TRAP_EXIT_ADDONS="./.tsendmail.sh ./.t.mbox"

   touch ./.t.mbox
   "${SNAIL}" ${ARGS} -ef ./.t.mbox
   echo ${?} > "${MBOX}"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!/bin/sh -
		(echo 'From Alchemilla Wed Apr 07 17:03:33 2017' && ${cat} && echo
			) >> "./.t.mbox"
	_EOT
   chmod 0755 ./.tsendmail.sh
   printf 'm me@exam.ple\nLine 1.\nHello.\n~.\n' |
   "${SNAIL}" ${ARGS} -Smta=./.tsendmail.sh
   printf 'm you@exam.ple\nLine 1.\nBye.\n~.\n' |
   "${SNAIL}" ${ARGS} -Smta=./.tsendmail.sh

   "${SNAIL}" ${ARGS} -ef ./.t.mbox
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -efL @t@me ./.t.mbox
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -efL @t@you ./.t.mbox
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -efL '@>@Line 1' ./.t.mbox
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -efL '@>@Hello.' ./.t.mbox
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -efL '@>@Bye.' ./.t.mbox
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -efL '@>@Good bye.' ./.t.mbox
   echo ${?} >> "${MBOX}"

   "${SNAIL}" ${ARGS} -fH ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -fL @t@me ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -fL @t@you ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -fL '@>@Line 1' ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -fL '@>@Hello.' ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -fL '@>@Bye.' ./.t.mbox >> "${MBOX}"
   echo ${?} >> "${MBOX}"
   "${SNAIL}" ${ARGS} -fL '@>@Good bye.' ./.t.mbox >> "${MBOX}" 2>/dev/null
   echo ${?} >> "${MBOX}"

   ${rm} -f ${TRAP_EXIT_ADDONS}
   TRAP_EXIT_ADDONS=

#1
#0
#0
#0
#0
#0
#0
#1
#>N  1 Alchemilla         1996-10-02 01:50    7/112                              
# N  2 Alchemilla         1996-10-02 01:50    7/111                              
#0
#>N  1 Alchemilla         1996-10-02 01:50    7/112                              
#0
# N  2 Alchemilla         1996-10-02 01:50    7/111                              
#0
#>N  1 Alchemilla         1996-10-02 01:50    7/112                              
# N  2 Alchemilla         1996-10-02 01:50    7/111                              
#0
#>N  1 Alchemilla         1996-10-02 01:50    7/112                              
#0
# N  2 Alchemilla         1996-10-02 01:50    7/111                              
#0
#0
   cksum_test behave:e_H_L_opts "${MBOX}" '1708955574 678'
}

t_behave_compose_hooks() {
   TRAP_EXIT_ADDONS="./.tsendmail.sh ./.tnotes"

   ${cat} <<-_EOT > ./.tsendmail.sh
		#!/bin/sh -
		${rm} -f "${MBOX}"
		(echo 'From PrimulaVeris Wed Apr 10 22:59:00 2017' && ${cat}) > "${MBOX}"
	_EOT
   chmod 0755 ./.tsendmail.sh

   printf 'm hook-test@exam.ple\nbody\n~.\nvar t_oce t_ocs t_ocs_shell t_ocl' |
   "${SNAIL}" ${ARGS} \
      -Smta=./.tsendmail.sh \
      -X'
         define t_ocs {
            read ver
            echo t_ocs
            echo "~^header list"; read hl; vput vexpr es substr "$hl" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to header list, aborting send"; echo "~x"
            endif
            echo "~^header insert cc splicy diet <splice@exam.ple> spliced";\
               read es; vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be diet, aborting send"; echo "~x"
            endif
            echo "~^header insert bcc juicy juice <juice@exam.ple> spliced";\
               read es; vput vexpr es substr "$es" 0 1
            if [ "$es" != 2 ]
               echoerr "Failed to be juicy, aborting send"; echo "~x"
            endif
            echo "~:set t_ocs"
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
      ' > ./.tnotes
   ex0_test behave:compose_hooks
   ${cat} ./.tnotes >> "${MBOX}"

   ${rm} -f ${TRAP_EXIT_ADDONS}
   TRAP_EXIT_ADDONS=

#From PrimulaVeris Wed Apr 10 22:59:00 2017
#Date: Wed, 02 Oct 1996 01:50:07 +0000
#To: hook-test@exam.ple, shell@exam.ple
#Cc: ocl@exam.ple, splicy diet spliced <splice@exam.ple>
#Bcc: juicy juice spliced <juice@exam.ple>, oce@exam.ple
#
#body
#t_ocs-shell
#t_ocs
##variable not set: t_oce
##variable not set: t_ocs
##variable not set: t_ocs_shell
##variable not set: t_ocl
   cksum_test behave:compose_hooks "${MBOX}" '3240856112 319'
}

t_behave_smime() { # FIXME add test/ dir, unroll tests therein
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

   printf "behave:s/mime:sign/verify: "
   echo bla | "${SNAIL}" ${ARGS} \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -s 'S/MIME test' ./.VERIFY
   printf 'verify\nx\n' |
   "${SNAIL}" ${ARGS} \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -Serrexit -R \
      -f ./.VERIFY >/dev/null 2>&1
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      printf 'error: verification failed\n'
      ESTAT=1
      ${rm} -f ${TRAP_EXIT_ADDONS}
      TRAP_EXIT_ADDONS=
      return
   fi
   printf ' .. disproof via openssl smime(1): '
   if openssl smime -verify -CAfile ./.tcert.pem \
         -in ./.VERIFY >/dev/null 2>&1; then
      printf 'ok\n'
   else
      printf 'failed\n'
      ESTAT=1
      ${rm} -f ${TRAP_EXIT_ADDONS}
      TRAP_EXIT_ADDONS=
      return
   fi

   # (signing +) encryption / decryption
   ${cat} <<-_EOT > ./.tsendmail.sh
		#!/bin/sh -
      ${rm} -f ./.ENCRYPT
		(echo 'From S-Postman Thu May 10 20:40:54 2012' && ${cat}) > ./.ENCRYPT
	_EOT
   chmod 0755 ./.tsendmail.sh

   printf "behave:s/mime:encrypt+sign/decrypt+verify: "
   echo bla |
   "${SNAIL}" ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./.tpair.pem \
      -Smta=./.tsendmail.sh \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -s 'S/MIME test' recei@ver.com
   # TODO CHECK
   printf 'decrypt ./.DECRYPT\nfi ./.DECRYPT\nverify\nx\n' |
   "${SNAIL}" ${ARGS} \
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
      printf 'error: decryption+verification failed\n'
   fi
   printf ' ..disproof via openssl smime(1): '
   if (openssl smime -decrypt -inkey ./.tkey.pem -in ./.ENCRYPT |
         openssl smime -verify -CAfile ./.tcert.pem) >/dev/null 2>&1; then
      printf 'ok\n'
   else
      printf 'failed\n'
      ESTAT=1
   fi
   ${sed} -e '/^Date:/d' -e '/^X-Decoding-Date/d' \
         -e \
         '/^Content-Disposition: attachment; filename="smime.p7s"/,/^-- /d' \
      < ./.DECRYPT > ./.ENCRYPT
   cksum_test ".. checksum of decrypted content" "./.ENCRYPT" '3090916509 510'

   printf "behave:s/mime:encrypt/decrypt: "
   ${rm} -f ./.DECRYPT
   echo bla | "${SNAIL}" ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./.tpair.pem \
      -Smta=./.tsendmail.sh \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Sfrom=test@localhost \
      -s 'S/MIME test' recei@ver.com
   printf 'decrypt ./.DECRYPT\nx\n' | "${SNAIL}" ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./.tpair.pem \
      -Smta=./.tsendmail.sh \
      -Ssmime-ca-file=./.tcert.pem -Ssmime-sign-cert=./.tpair.pem \
      -Sfrom=test@localhost \
      -Serrexit -R \
      -f ./.ENCRYPT >/dev/null 2>&1
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'error: decryption failed\n'
   fi
   printf '.. disproof via openssl smime(1): '
   if openssl smime -decrypt -inkey ./.tkey.pem \
         -in ./.ENCRYPT >/dev/null 2>&1; then
      printf 'ok\n'
   else
      printf 'failed\n'
      ESTAT=1
   fi
   ${sed} -e '/^Date:/d' -e '/^X-Decoding-Date/d' \
      < ./.DECRYPT > ./.ENCRYPT
   cksum_test ".. checksum of decrypted content" ./.ENCRYPT '999887248 295'

   ${rm} -f ${TRAP_EXIT_ADDONS}
   TRAP_EXIT_ADDONS=
}

# t_content()
# Some basic tests regarding correct sending of mails, via STDIN / -t / -q,
# including basic MIME Content-Transfer-Encoding correctness (quoted-printable)
# Note we unfortunately need to place some statements without proper
# indentation because of continuation problems
t_content() {
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
   < "${BODY}" "${SNAIL}" ${ARGS} ${ADDARG_UNI} \
      -a "${BODY}" -s "${SUB}" "${MBOX}"
   ex0_test content:001
   cksum_test content:001 "${MBOX}" '2356108758 6413'

   ${rm} -f "${MBOX}"
   < /dev/null "${SNAIL}" ${ARGS} ${ADDARG_UNI} \
      -a "${BODY}" -s "${SUB}" -q "${BODY}" "${MBOX}"
   ex0_test content:002
   cksum_test content:002 "${MBOX}" '2356108758 6413'

   ${rm} -f "${MBOX}"
   (  echo "To: ${MBOX}" && echo "Subject: ${SUB}" && echo &&
      ${cat} "${BODY}"
   ) | "${SNAIL}" ${ARGS} ${ADDARG_UNI} -Snodot -a "${BODY}" -t
   ex0_test content:003
   cksum_test content:003 "${MBOX}" '2356108758 6413'

   # Test for [260e19d] (Juergen Daubert)
   ${rm} -f "${MBOX}"
   echo body | "${SNAIL}" ${ARGS} "${MBOX}"
   ex0_test content:004
   cksum_test content:004 "${MBOX}" '4004005686 49'

   # Sending of multiple mails in a single invocation
   ${rm} -f "${MBOX}"
   (  printf "m ${MBOX}\n~s subject1\nE-Mail Körper 1\n~.\n" &&
      printf "m ${MBOX}\n~s subject2\nEmail body 2\n~.\n" &&
      echo x
   ) | "${SNAIL}" ${ARGS} ${ADDARG_UNI}
   ex0_test content:005
   cksum_test content:005 "${MBOX}" '2157252578 260'

   ## $BODY CHANGED

   # "Test for" [d6f316a] (Gavin Troy)
   ${rm} -f "${MBOX}"
   printf "m ${MBOX}\n~s subject1\nEmail body\n~.\nfi ${MBOX}\np\nx\n" |
   "${SNAIL}" ${ARGS} ${ADDARG_UNI} -Spipe-text/plain="${cat}" > "${BODY}"
   ex0_test content:006
   ${sed} -e 1d < "${BODY}" > "${MBOX}"
   cksum_test content:006 "${MBOX}" '2273863401 83'

   # "Test for" [c299c45] (Peter Hofmann) TODO shouldn't end up QP-encoded?
   ${rm} -f "${MBOX}"
   ${awk} 'BEGIN{
      for(i = 0; i < 10000; ++i)
         printf "\xC3\xBC"
         #printf "\xF0\x90\x87\x90"
      }' | "${SNAIL}" ${ARGS} ${ADDARG_UNI} -s TestSubject "${MBOX}"
   ex0_test content:007
   cksum_test content:007 "${MBOX}" '1754234717 61767'

   ## Test some more corner cases for header bodies (as good as we can today) ##

   #
   ${rm} -f "${MBOX}"
   echo | "${SNAIL}" ${ARGS} ${ADDARG_UNI} \
      -s 'a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲' \
      "${MBOX}"
   ex0_test content:008
   cksum_test content:008 "${MBOX}" '1563381297 326'

   # Single word (overlong line split -- bad standard! Requires injection of
   # artificial data!!  Bad can be prevented by using RFC 2047 encoding)
   ${rm} -f "${MBOX}"
   i=`${awk} 'BEGIN{for(i=0; i<92; ++i) printf "0123456789_"}'`
   echo | "${SNAIL}" ${ARGS} -s "${i}" "${MBOX}"
   ex0_test content:009
   cksum_test content:009 "${MBOX}" '1996714851 1669'

   # Combination of encoded words, space and tabs of varying sort
   ${rm} -f "${MBOX}"
   echo | "${SNAIL}" ${ARGS} ${ADDARG_UNI} \
      -s "1Abrä Kaspas1 2Abra Katä	b_kaspas2  \
3Abrä Kaspas3   4Abrä Kaspas4    5Abrä Kaspas5     \
6Abra Kaspas6      7Abrä Kaspas7       8Abra Kaspas8        \
9Abra Kaspastäb4-3 	 	 	 10Abra Kaspas1 _ 11Abra Katäb1	\
12Abra Kadabrä1 After	Tab	after	Täb	this	is	NUTS" \
      "${MBOX}"
   ex0_test content:010
   cksum_test content:010 "${MBOX}" '2956039469 542'

   # Overlong multibyte sequence that must be forcefully split
   # todo This works even before v15.0, but only by accident
   ${rm} -f "${MBOX}"
   echo | "${SNAIL}" ${ARGS} ${ADDARG_UNI} \
      -s "✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄" \
      "${MBOX}"
   ex0_test content:011
   cksum_test content:011 "${MBOX}" '454973928 610'

   # Trailing WS
   ${rm} -f "${MBOX}"
   echo | "${SNAIL}" ${ARGS} \
      -s "1-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-5 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-6 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      "${MBOX}"
   ex0_test content:012
   cksum_test content:012 "${MBOX}" '1014122962 248'

   # Leading and trailing WS
   ${rm} -f "${MBOX}"
   echo | "${SNAIL}" ${ARGS} \
      -s "	 	 2-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      "${MBOX}"
   ex0_test content:013
   cksum_test content:013 "${MBOX}" '3212167908 187'

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
   echo bla | "${SNAIL}" ${ARGS} ${ADDARG_UNI} \
      -a "./.ttt/ma'ger.txt" -a "./.ttt/mä'ger.txt" \
      -a './.ttt/diet\ is \curd.txt' -a './.ttt/diet "is" curd.txt' \
      -a ./.ttt/höde-tröge.txt \
      -a ./.ttt/höde__tröge__müde__dätte__hätte__vülle__gülle__äse__äße__säuerliche__kräuter__österliche__grüße__mäh.txt \
      -a ./.ttt/höde__tröge__müde__dätte__hätte__vuelle__guelle__aese__aesse__sauerliche__kräuter__österliche__grüße__mäh.txt \
      -a ./.ttt/hööööööööööööööööö_nöööööööööööööööööööööö_düüüüüüüüüüüüüüüüüüü_bäääääääääääääääääääääääh.txt \
      -a ./.ttt/✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆.txt \
      "${MBOX}"
   ex0_test content:014-1
   ${rm} -rf ./.ttt
   cksum_test content:014-1 "${MBOX}" '589846634 2491'
   # `resend' test
   printf "Resend ${BODY}\nx\n" | "${SNAIL}" ${ARGS} -f "${MBOX}"
   ex0_test content:014-2
   cksum_test content:014-2 "${MBOX}" '589846634 2491'
}

t_all() {
   if have_feat devel; then
      ARGS="${ARGS} -Smemdebug"
      export ARGS
   fi
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
