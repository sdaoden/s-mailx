#!/bin/sh -
#@ Usage: ./cc-test.sh [--check-only [s-nail-binary]]

SNAIL=./s-nail
ARGS='-n# -Sstealthmua -Snosave -Sexpandaddr=restrict -Sdotlock-ignore-error'
CONF=./make.rc
BODY=./.cc-body.txt
MBOX=./.cc-test.mbox

MAKE="${MAKE:-`command -v make`}"
awk=${awk:-`command -v awk`}
cat=${cat:-`command -v cat`}
cksum=${cksum:-`command -v cksum`}
rm=${rm:-`command -v rm`}
sed=${sed:-`command -v sed`}
grep=${grep:-`command -v grep`}

##  --  >8  --  8<  --  ##

export SNAIL ARGS CONF BODY MBOX MAKE awk cat cksum rm sed grep

# NOTE!  UnixWare 7.1.4 gives ISO-10646-Minimum-European-Subset for
# nl_langinfo(CODESET), then, so also overwrite ttycharset.
# (In addition this setup allows us to succeed on TinyCore 4.4 that has no
# other locales than C/POSIX installed by default!)
LC=en_US.UTF-8
LC_ALL=${LC} LANG=${LC}
ttycharset=UTF-8
export LC_ALL LANG ttycharset

# Problem: force $SHELL to be a real shell.  It seems some testing environments
# use nologin(?), but we need a real shell for command execution
if { echo ${SHELL} | ${grep} nologin; } >/dev/null 2>&1; then
   echo >&2 '$SHELL seems to be nologin, overwriting to /bin/sh!'
   SHELL=/bin/sh
   export SHELL
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

# cc_all_configs()
# Test all configs TODO doesn't cover all *combinations*, stupid!
cc_all_configs() {
   < ${CONF} ${awk} '
      BEGIN {
         NOTME["WANT_AUTOCC"] = 1
         NOTME["WANT_DEBUG"] = 1
         NOTME["WANT_DEVEL"] = 1
         NOTME["WANT_NOEXTMD5"] = 1
         NOTME["WANT_NOALLOCA"] = 1
         NOTME["WANT_NOMEMDBG"] = 1
         NOTME["WANT_NYD2"] = 1
         i = 0
      }
      /^[[:space:]]*WANT_/ {
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
            printf "WANT_AUTOCC=1\n"
         }
         for (j = 1; j < i; ++j) {
            for (k = 1; k < j; ++k)
               printf data[k] "=0 "
            for (k = j; k < i; ++k)
               printf data[k] "=1 "
            printf "WANT_AUTOCC=1\n"
         }
         # With debug
         for (j = 1; j < i; ++j) {
            for (k = 1; k < j; ++k)
               printf data[k] "=1 "
            for (k = j; k < i; ++k)
               printf data[k] "=0 "
            printf "WANT_AUTOCC=1\n"
            printf "WANT_DEBUG=1\n"
         }
         for (j = 1; j < i; ++j) {
            for (k = 1; k < j; ++k)
               printf data[k] "=0 "
            for (k = j; k < i; ++k)
               printf data[k] "=1 "
            printf "WANT_AUTOCC=1\n"
            printf "WANT_DEBUG=1\n"
         }

         printf "CONFIG=NULL WANT_AUTOCC=0\n"
         printf "CONFIG=NULL WANT_AUTOCC=1\n"
         printf "CONFIG=NULLI WANT_AUTOCC=0\n"
         printf "CONFIG=NULLI WANT_AUTOCC=1\n"
         printf "CONFIG=MINIMAL WANT_AUTOCC=0\n"
         printf "CONFIG=MINIMAL WANT_AUTOCC=1\n"
         printf "CONFIG=MEDIUM WANT_AUTOCC=0\n"
         printf "CONFIG=MEDIUM WANT_AUTOCC=1\n"
         printf "CONFIG=NETSEND WANT_AUTOCC=0\n"
         printf "CONFIG=NETSEND WANT_AUTOCC=1\n"
         printf "CONFIG=MAXIMAL WANT_AUTOCC=0\n"
         printf "CONFIG=MAXIMAL WANT_AUTOCC=1\n"
         printf "CONFIG=DEVEL WANT_AUTOCC=0\n"
         printf "CONFIG=DEVEL WANT_AUTOCC=1\n"
         printf "CONFIG=ODEVEL WANT_AUTOCC=0\n"
         printf "CONFIG=ODEVEL WANT_AUTOCC=1\n"
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
   csum="`${sed} -e '/^From /d' -e '/^Date: /d' \
         -e '/^ boundary=/d' -e '/^--=-=/d' < \"${f}\" \
         -e '/^\[-- Message/d' | ${cksum}`";
   if [ "${csum}" = "${s}" ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'error: checksum mismatch (got %s)\n' "${csum}"
   fi
}

have_feat() {
   (
   echo 'feat' |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} |
   ${grep} ${1}
   ) >/dev/null 2>&1
}

# t_behave()
# Basic (easily testable) behaviour tests
t_behave() {
   # Test for [d1f1a19]
   ${rm} -f "${MBOX}"
   printf 'echo +nix\nset folder=/\necho +nix\nset nofolder\necho +nix\nx' |
      MAILRC=/dev/null "${SNAIL}" ${ARGS} > "${MBOX}"
   cksum_test behave:001 "${MBOX}" '4214021069 15'

   # POSIX: setting *noprompt*/prompt='' shall prevent prompting TODO
   # TODO for this to be testable we need a way to echo a variable
   # TODO or to force echo of the prompt

   __behave_ifelse

   # FIXME __behave_alias

   # FIXME __behave_mlist

   have_feat SSL/TLS && have_feat S/MIME && __behave_smime
}

__behave_ifelse() {
   # Nestable conditions test
   ${rm} -f "${MBOX}"
   ${cat} <<- '__EOT' | MAILRC=/dev/null "${SNAIL}" ${ARGS} > "${MBOX}"
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
		if $dietcurd < 11
		   echo 11.ok1
		   if $dietcurd > 9
		      echo 11.ok2
		   else
		      echo 11.err2
		   endif
		   if $dietcurd == 10
		      echo 11.ok3
		   else
		      echo 11.err3
		   endif
		   if $dietcurd >= 10
		      echo 11.ok4
		   else
		      echo 11.err4
		   endif
		   if $dietcurd <= 10
		      echo 11.ok5
		   else
		      echo 11.err5
		   endif
		   if $dietcurd >= 11
		      echo 11.err6
		   else
		      echo 11.ok6
		   endif
		   if $dietcurd <= 9
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
   cksum_test behave:if-normal "${MBOX}" '557629289 631'

   if have_feat REGEX; then
      ${rm} -f "${MBOX}"
      ${cat} <<- '__EOT' | MAILRC=/dev/null "${SNAIL}" ${ARGS} > "${MBOX}"
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
      cksum_test behave:if-regex "${MBOX}" '439960016 81'
   fi
}

__behave_smime() { # FIXME add test/ dir, unroll tests therein, regular enable!
   printf 'behave:s/mime: .. generating test key and certificate ..\n'
   ${cat} <<-_EOT > ./t.conf
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
   openssl req -x509 -nodes -days 3650 -config ./t.conf \
      -newkey rsa:1024 -keyout ./tkey.pem -out ./tcert.pem >/dev/null 2>&1
   ${rm} -f ./t.conf
   ${cat} ./tkey.pem ./tcert.pem > ./tpair.pem

   printf "behave:s/mime:sign/verify: "
   echo bla |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -Ssmime-ca-file=./tcert.pem -Ssmime-sign-cert=./tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -s 'S/MIME test' ./VERIFY
   # TODO CHECK
   printf 'verify\nx\n' |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -Ssmime-ca-file=./tcert.pem -Ssmime-sign-cert=./tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -Sbatch-exit-on-error -R \
      -f ./VERIFY >/dev/null 2>&1
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'error: verification failed\n'
      ${rm} -f ./VERIFY ./tkey.pem ./tcert.pem ./tpair.pem
      return
   fi
   ${rm} -rf ./VERIFY

   # (signing +) encryption / decryption
   ${cat} <<-_EOT > ./tsendmail.sh
		#!/bin/sh -
		(echo 'From S-Postman Thu May 10 20:40:54 2012' && ${cat}) > ./ENCRYPT
	_EOT
   chmod 0755 ./tsendmail.sh

   printf "behave:s/mime:encrypt+sign/decrypt+verify: "
   echo bla |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./tpair.pem \
      -Ssendmail=./tsendmail.sh \
      -Ssmime-ca-file=./tcert.pem -Ssmime-sign-cert=./tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -s 'S/MIME test' recei@ver.com
   # TODO CHECK
   printf 'decrypt ./DECRYPT\nfi ./DECRYPT\nverify\nx\n' |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./tpair.pem \
      -Ssendmail=./tsendmail.sh \
      -Ssmime-ca-file=./tcert.pem -Ssmime-sign-cert=./tpair.pem \
      -Ssmime-sign -Sfrom=test@localhost \
      -Sbatch-exit-on-error -R \
      -f ./ENCRYPT >/dev/null 2>&1
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'error: decryption+verification failed\n'
   fi
   ${sed} -e '/^X-Decoding-Date/d' \
         -e \
         '/^Content-Disposition: attachment; filename="smime.p7s"/,/^-- /d' \
      < ./DECRYPT > ./ENCRYPT
   cksum_test ".. checksum of decrypted content" "./ENCRYPT" '82649489 454'

   ${rm} -f ./DECRYPT
   printf "behave:s/mime:encrypt/decrypt: "
   echo bla |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./tpair.pem \
      -Ssendmail=./tsendmail.sh \
      -Ssmime-ca-file=./tcert.pem -Ssmime-sign-cert=./tpair.pem \
      -Sfrom=test@localhost \
      -s 'S/MIME test' recei@ver.com
   printf 'decrypt ./DECRYPT\nx\n' |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -Ssmime-force-encryption \
      -Ssmime-encrypt-recei@ver.com=./tpair.pem \
      -Ssendmail=./tsendmail.sh \
      -Ssmime-ca-file=./tcert.pem -Ssmime-sign-cert=./tpair.pem \
      -Sfrom=test@localhost \
      -Sbatch-exit-on-error -R \
      -f ./ENCRYPT >/dev/null 2>&1
   if [ $? -eq 0 ]; then
      printf 'ok\n'
   else
      ESTAT=1
      printf 'error: decryption failed\n'
      # FALLTHRU
   fi
   ${sed} -e '/^X-Decoding-Date/d' \
      < ./DECRYPT > ./ENCRYPT
   cksum_test ".. checksum of decrypted content" "./ENCRYPT" '2694938815 239'

   ${rm} -f ./tsendmail.sh ./ENCRYPT ./DECRYPT \
      ./tkey.pem ./tcert.pem ./tpair.pem
}

# t_content()
# Some basic tests regarding correct sending of mails, via STDIN / -t / -q,
# including basic MIME Content-Transfer-Encoding correctness (quoted-printable)
# Note we unfortunately need to place some statements without proper
# indentation because of continuation problems
t_content() {
   ${rm} -f "${BODY}" "${MBOX}"

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

   # TODO Note: because of our weird putline() handling in <-> collect.c
   ${rm} -f "${MBOX}"
   < "${BODY}" MAILRC=/dev/null \
   "${SNAIL}" -nSstealthmua -Sexpandaddr -a "${BODY}" -s "${SUB}" "${MBOX}"
   cksum_test content:001-0 "${MBOX}" '3310338268 6375'

   ${rm} -f "${MBOX}"
   < "${BODY}" MAILRC=/dev/null \
   "${SNAIL}" ${ARGS} -Snodot -a "${BODY}" -s "${SUB}" "${MBOX}"
   cksum_test content:001 "${MBOX}" '62505451 6374'

   ${rm} -f "${MBOX}"
   < /dev/null MAILRC=/dev/null \
   "${SNAIL}" ${ARGS} -a "${BODY}" -s "${SUB}" \
      -q "${BODY}" "${MBOX}"
   cksum_test content:002 "${MBOX}" '3310338268 6375'

   ${rm} -f "${MBOX}"
   (  echo "To: ${MBOX}" && echo "Subject: ${SUB}" && echo &&
      ${cat} "${BODY}"
   ) | MAILRC=/dev/null "${SNAIL}" ${ARGS} -Snodot -a "${BODY}" -t
   cksum_test content:003 "${MBOX}" '62505451 6374'

   # Test for [260e19d] (Juergen Daubert)
   ${rm} -f "${MBOX}"
   echo body | MAILRC=/dev/null "${SNAIL}" ${ARGS} "${MBOX}"
   cksum_test content:004 "${MBOX}" '3729232114 11'

   # Sending of multiple mails in a single invocation
   ${rm} -f "${MBOX}"
   (  printf "m ${MBOX}\n~s subject1\nE-Mail Körper 1\n~.\n" &&
      printf "m ${MBOX}\n~s subject2\nEmail body 2\n~.\n" &&
      echo x
   ) | MAILRC=/dev/null "${SNAIL}" ${ARGS}
   cksum_test content:005 "${MBOX}" '773028641 184'

   ## $BODY CHANGED

   # "Test for" [d6f316a] (Gavin Troy)
   ${rm} -f "${MBOX}"
   printf "m ${MBOX}\n~s subject1\nEmail body\n~.\nfi ${MBOX}\np\nx\n" |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -Spipe-text/plain="${cat}" > "${BODY}"
   ${sed} -e 1d < "${BODY}" > "${MBOX}"
   cksum_test content:006 "${MBOX}" '654030565 45'

   # "Test for" [c299c45] (Peter Hofmann) TODO shouldn't end up QP-encoded?
   # TODO Note: because of our weird putline() handling in <-> collect.c
   ${rm} -f "${MBOX}"
   LC_ALL=C ${awk} 'BEGIN{
      for(i = 0; i < 10000; ++i)
         printf "\xC3\xBC"
         #printf "\xF0\x90\x87\x90"
      }' |
   MAILRC=/dev/null "${SNAIL}" -nSstealthmua -Sexpandaddr \
      -s TestSubject "${MBOX}"
   cksum_test content:007-0 "${MBOX}" '2747333583 61729'

   ${rm} -f "${MBOX}"
   LC_ALL=C ${awk} 'BEGIN{
      for(i = 0; i < 10000; ++i)
         printf "\xC3\xBC"
         #printf "\xF0\x90\x87\x90"
      }' |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} -s TestSubject "${MBOX}"
   cksum_test content:007 "${MBOX}" '3343002941 61728'

   ## Test some more corner cases for header bodies (as good as we can today) ##

   #
   ${rm} -f "${MBOX}"
   echo |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -s 'a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲a̲b̲c̲d̲e̲f̲h̲i̲k̲l̲m̲n̲o̲r̲s̲t̲u̲v̲w̲x̲z̲' \
      "${MBOX}"
   cksum_test content:008 "${MBOX}" '3872015771 288'

   # Single word (overlong line split -- bad standard! Requires injection of
   # artificial data!!  Bad can be prevented by using RFC 2047 encoding)
   ${rm} -f "${MBOX}"
   i=`LC_ALL=C ${awk} 'BEGIN{for(i=0; i<92; ++i) printf "0123456789_"}'`
   echo | MAILRC=/dev/null "${SNAIL}" ${ARGS} -s "${i}" "${MBOX}"
   cksum_test content:009 "${MBOX}" '2048460448 1631'

   # Combination of encoded words, space and tabs of varying sort
   ${rm} -f "${MBOX}"
   echo | MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -s "1Abrä Kaspas1 2Abra Katä	b_kaspas2  \
3Abrä Kaspas3   4Abrä Kaspas4    5Abrä Kaspas5     \
6Abra Kaspas6      7Abrä Kaspas7       8Abra Kaspas8        \
9Abra Kaspastäb4-3 	 	 	 10Abra Kaspas1 _ 11Abra Katäb1	\
12Abra Kadabrä1 After	Tab	after	Täb	this	is	NUTS" \
      "${MBOX}"
   cksum_test content:010 "${MBOX}" '1272213842 504'

   # Overlong multibyte sequence that must be forcefully split
   # todo This works even before v15.0, but only by accident
   ${rm} -f "${MBOX}"
   echo | MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -s "✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄\
✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄✄" \
      "${MBOX}"
   cksum_test content:011 "${MBOX}" '2972351879 572'

   # Trailing WS
   ${rm} -f "${MBOX}"
   echo |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -s "1-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-5 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-6 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      "${MBOX}"
   cksum_test content:012 "${MBOX}" '2467265470 210'

   # Leading and trailing WS
   ${rm} -f "${MBOX}"
   echo |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} \
      -s "	 	 2-1 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-2 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-3 	 B2 	 B3 	 B4 	 B5 	 B6 	 B\
1-4 	 B2 	 B3 	 B4 	 B5 	 B6 	 " \
      "${MBOX}"
   cksum_test content:013 "${MBOX}" '4119922611 149'

   # Quick'n dirty RFC 2231 test; i had more when implementing it, but until we
   # have a (better) test framework materialize a quick shot
   ${rm} -f "${MBOX}"
   : > "ma'ger.txt"
   : > "mä'ger.txt"
   : > 'diet\ is \curd.txt'
   : > diet \"is\" curd.txt
   : > höde-tröge.txt
   : > höde__tröge__müde__dätte__hätte__vülle__gülle__äse__äße__säuerliche__kräuter__österliche__grüße__mäh.txt
   : > höde__tröge__müde__dätte__hätte__vuelle__guelle__aese__aesse__sauerliche__kräuter__österliche__grüße__mäh.txt
   : > hööööööööööööööööö_nöööööööööööööööööööööö_düüüüüüüüüüüüüüüüüüü_bäääääääääääääääääääääääh.txt
   : > ✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆.txt
   echo bla |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} -Snodot \
      -a "ma'ger.txt" -a "mä'ger.txt" \
      -a 'diet\\\ is\ \\curd.txt' -a diet \"is\" curd.txt \
      -a höde-tröge.txt \
      -a höde__tröge__müde__dätte__hätte__vülle__gülle__äse__äße__säuerliche__kräuter__österliche__grüße__mäh.txt \
      -a höde__tröge__müde__dätte__hätte__vuelle__guelle__aese__aesse__sauerliche__kräuter__österliche__grüße__mäh.txt \
      -a hööööööööööööööööö_nöööööööööööööööööööööö_düüüüüüüüüüüüüüüüüüü_bäääääääääääääääääääääääh.txt \
      -a ✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆.txt \
      "${MBOX}"
   ${rm} -f "ma'ger.txt" "mä'ger.txt" 'diet\ is \curd.txt' \
      diet \"is\" curd.txt höde-tröge.txt \
      höde__tröge__müde__dätte__hätte__vülle__gülle__äse__äße__säuerliche__kräuter__österliche__grüße__mäh.txt \
      höde__tröge__müde__dätte__hätte__vuelle__guelle__aese__aesse__sauerliche__kräuter__österliche__grüße__mäh.txt \
      hööööööööööööööööö_nöööööööööööööööööööööö_düüüüüüüüüüüüüüüüüüü_bäääääääääääääääääääääääh.txt \
      ✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆✆.txt
   cksum_test content:14 "${MBOX}" '1106643854 2453'
   # `resend' test
   printf "Resend ${BODY}\nx\n" |
   MAILRC=/dev/null "${SNAIL}" ${ARGS} -f "${MBOX}"
   cksum_test content:14-2 "${MBOX}" '1106643854 2453'

   ${rm} -f "${BODY}" "${MBOX}"
}

t_all() {
   if have_feat DEVEL; then
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
