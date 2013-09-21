#!/bin/sh -
#@ XXX Add tests

# NOTE!  UnixWare 7.1.4 gives ISO-10646-Minimum-European-Subset for
# nl_langinfo(CODESET), then, so also overwrite ttycharset.
# (In addition this setup allows us to succeed on TinyCore 4.4 that has no
# other locales than C/POSIX installed by default!)
LC=en_US.UTF-8
LC_ALL=${LC} LANG=${LC}
ttycharset=UTF-8
export LC_ALL LANG ttycharset

MAKE=make
NAIL=./s-nail
CONF=./conf.rc

OUT=./.cc-test.out
ERR=./.cc-test.err
BODY=./.cc-body.txt
MBOX=./.cc-test.mbox
ESTAT=0

rm -f "${OUT}" "${ERR}" "${BODY}" "${MBOX}" 2>> "${ERR}"

# Test all configs
cc_all_configs() {
   < ${CONF} awk '
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
   done >> "${OUT}" 2>> "${ERR}"
}

# Test a UTF-8 mail as a whole via -t, and in pieces (without -t ;)
cksum_test() {
   f=$1 s=$2 tno=$3
   [ "`sed -e '/^From /d' -e '/^Date: /d' \
         -e '/^ boundary=/d' -e /^--=_/d < \"${f}\" | \
         cksum`" != "${s}" ] && {
      ESTAT=1
      echo "Checksum mismatch test ${tno}: ${f}" 2>> "${ERR}"
   }
}

test_mail() {
   printf "\n\n########################################\n\n" >> "${OUT}"
   printf "\n\n########################################\n\n" >> "${ERR}"
   "${MAKE}" >> "${OUT}" 2>> "${ERR}"

   # Two tests for MIME-CTE and (a bit) content classification
   rm -f "${MBOX}"
   < "${BODY}" MAILRC=/dev/null \
   "${NAIL}" -n -Sstealthmua -a "${BODY}" -s "${SUB}" "${MBOX}"
   cksum_test "${MBOX}" '2606934084 5649' 1

   rm -f "${MBOX}"
   (  echo "To: ${MBOX}" && echo "Subject: ${SUB}" && echo &&
      cat "${BODY}"
   ) | MAILRC=/dev/null "${NAIL}" -n -Sstealthmua -a "${BODY}" -t
   cksum_test "${MBOX}" '799758423 5648' 2

   # Test for [260e19d].  Juergen Daubert.
   rm -f "${MBOX}"
   echo body | MAILRC=/dev/null "${NAIL}" -n -Sstealthmua "${MBOX}"
   cksum_test "${MBOX}" '506144051 104' 3

   # Sending of multiple mails in a single invocation
   rm -f "${MBOX}"
   (  printf "m ${MBOX}\n~s subject1\nE-Mail Körper 1\n.\n" &&
      printf "m ${MBOX}\n~s subject2\nEmail body 2\n.\n" &&
      echo x
   ) | MAILRC=/dev/null "${NAIL}" -N -n -# -Sstealthmua
   cksum_test "${MBOX}" '2028749685 277' 4
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

cc_all_configs
test_mail

if [ ${ESTAT} -eq 0 ]; then
   echo 'Everything seems to be fine around here' >> "${OUT}" 2>> "${ERR}"
   "${MAKE}" distclean >> "${OUT}" 2>> "${ERR}"
   rm -f "${BODY}" "${MBOX}" >> "${OUT}" 2>> "${ERR}"
fi
exit ${ESTAT}
# vim:set fenc=utf8:s-it-mode
