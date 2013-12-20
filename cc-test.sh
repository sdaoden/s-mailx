#!/bin/sh -
#@ Usage: ./cc-test.sh [--check-only]
#@ XXX Add tests

NAIL=./s-nail
CONF=./conf.rc
BODY=./.cc-body.txt
MBOX=./.cc-test.mbox

awk=`command -pv awk`
cat=`command -pv cat`
cksum=`command -pv cksum`
MAKE="${MAKE:-`command -pv make`}"
rm=`command -pv rm`

##  --  >8  --  8<  --  ##

# NOTE!  UnixWare 7.1.4 gives ISO-10646-Minimum-European-Subset for
# nl_langinfo(CODESET), then, so also overwrite ttycharset.
# (In addition this setup allows us to succeed on TinyCore 4.4 that has no
# other locales than C/POSIX installed by default!)
LC=en_US.UTF-8
LC_ALL=${LC} LANG=${LC}
ttycharset=UTF-8
export LC_ALL LANG ttycharset

ESTAT=0

usage() {
      echo >&2 "Usage: ./cc-test.sh [--check-only [nail-binary]]"
      exit 1
}

CHECK_ONLY=
[ ${#} -gt 0 ] && {
   [ "${1}" = --check-only ] || usage
   [ ${#} -gt 2 ] && usage
   [ ${#} -eq 2 ] && NAIL="${2}"
   [ -x "${NAIL}" ] || usage
   CHECK_ONLY=1
}

rm -f "${BODY}" "${MBOX}"

# Test all configs TODO doesn't cover all *combinations*, stupid!
cc_all_configs() {
   < ${CONF} ${awk} '
   BEGIN {i = 0}
   /^[[:space:]]*WANT_/ {
      sub(/^[[:space:]]*/, "")
      sub(/=.*$/, "")
      data[i++] = $1
   }
   END {
      for (j = 0; j < i; ++j) {
         for (k = 0; k < j; ++k)
            printf data[k] "=1 "
         for (k = j; k < i; ++k)
            printf data[k] "=0 "
         printf "\n"
         for (k = 0; k < j; ++k)
            printf data[k] "=0 "
         for (k = j; k < i; ++k)
            printf data[k] "=1 "
         printf "\n"
      }
   }
   ' | while read c; do
      printf "\n\n##########\n$c\n"
      printf "\n\n##########\n$c\n" >&2
      sh -c "${MAKE} ${c}"
      ${MAKE} distclean
   done
}

# Test a UTF-8 mail as a whole via -t, and in pieces (without -t ;)
cksum_test() {
   tno=$1 f=$2 s=$3
   [ "`sed -e '/^From /d' -e '/^Date: /d' \
         -e '/^ boundary=/d' -e /^--=_/d < \"${f}\" | \
         cksum`" != "${s}" ] && {
      ESTAT=1
      echo >&2 "Checksum mismatch test ${tno}: ${f}"
   }
}

test_mail() {
   [ -n "${CHECK_ONLY}" ] || {
      printf "\n\n## test_mail() #########################\n\n"
      "${MAKE}"
   }

   # Three tests for MIME-CTE and (a bit) content classification.
   # At the same time testing -q FILE, < FILE and -t FILE
   ${rm} -f "${MBOX}"
   < "${BODY}" MAILRC=/dev/null \
   "${NAIL}" -n -Sstealthmua -a "${BODY}" -s "${SUB}" "${MBOX}"
   cksum_test 1 "${MBOX}" '2606934084 5649'

   ${rm} -f "${MBOX}"
   < /dev/null MAILRC=/dev/null \
   "${NAIL}" -n -Sstealthmua -a "${BODY}" -s "${SUB}" \
      -q "${BODY}" "${MBOX}"
   cksum_test 2 "${MBOX}" '2606934084 5649'

   ${rm} -f "${MBOX}"
   (  echo "To: ${MBOX}" && echo "Subject: ${SUB}" && echo &&
      ${cat} "${BODY}"
   ) | MAILRC=/dev/null "${NAIL}" -n -Sstealthmua -a "${BODY}" -t
   cksum_test 3 "${MBOX}" '799758423 5648'

   # Test for [260e19d].  Juergen Daubert.
   ${rm} -f "${MBOX}"
   echo body | MAILRC=/dev/null "${NAIL}" -n -Sstealthmua "${MBOX}"
   cksum_test 4 "${MBOX}" '506144051 104'

   # Sending of multiple mails in a single invocation
   ${rm} -f "${MBOX}"
   (  printf "m ${MBOX}\n~s subject1\nE-Mail Körper 1\n.\n" &&
      printf "m ${MBOX}\n~s subject2\nEmail body 2\n.\n" &&
      echo x
   ) | MAILRC=/dev/null "${NAIL}" -n -# -Sstealthmua
   cksum_test 5 "${MBOX}" '2028749685 277'
}

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
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.1\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.12\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.123\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.1234"\
"\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.1234"\
"5\n"\
"Ich bin eine ziemlich lange, steile, scharfe Zeile mit Unix Zeilenende.1234"\
"56\n"\
"=VIER = EQUAL SIGNS=ON A LINE=\n"\
" \n"\
"Die letzte Zeile war ein Leerschritt.\n"\
' '\
 > "${BODY}"

SUB='Äbrä  Kä?dä=brö 	 Fü?di=bus? '\
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

[ -n "${CHECK_ONLY}" ] || cc_all_configs
test_mail

[ ${ESTAT} -eq 0 ] && echo Ok || echo >&2 'Errors occurred'
[ -n "${CHECK_ONLY}" ] || "${MAKE}" distclean
${rm} -f "${BODY}" "${MBOX}"

exit ${ESTAT}
# vim:set fenc=utf8:s-it-mode
