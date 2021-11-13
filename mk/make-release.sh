#!/bin/sh -
#@ make-release.sh: simple somewhat generic release builder

# In order to be able to remove the release scripts from the release tarball,
# we must delete them, which some shells may not like while they are running.
# So be safe and move instances temporarily to .git/, the .inc will remove them
if [ "`basename \`pwd\``" != .git ]; then
   [ -d mk ] || exit 84

   cp mk/make-release.* .git/
   cd .git
   exec sh ./make-release.sh "${@}"
fi
cd ..

## Variables

: ${PROGRAM:=s-nail}
: ${UPROGRAM:=S-nail}
: ${MANUAL:=code-nail.html}

: ${UPLOAD:=steffen@vpn.sdaoden.eu:/var/www/localhost/downloads}

: ${MAILX:='s-nail -Snofollowup-to -Sreply-to=mailx -Ssmime-sign'}
: ${ACCOUNT:=ich}
: ${MAILBCC:=mailx-announce-bcc}
: ${MAILTO:=mailx-announce}

have_perl=
if command -v perl >/dev/null 2>&1; then
   have_perl=1
fi

if command -v s-nail >/dev/null 2>&1; then :; else
   echo >&2 'We need s-nail in the path in order to update errors etc.!'
   echo >&2 '(To create hashtables of those, to be exact.)'
   exit 1
fi

## Hooks

current_version() {
   VERSION=`VERSION= TOPDIR= \
         awk=${awk} grep=${grep} sed=${sed} cmp=${cmp} mv=${mv} \
         ${SHELL} mk/make-version.sh create`
}

update_stable_hook() {
   [ -n "${grappa}${have_perl}" ] || exit 86

   #
   echo 'gen-version.h: update'
   TOPDIR= awk=${awk} grep=${grep} sed=${sed} cmp=${cmp} mv=${mv} \
      ${SHELL} mk/make-version.sh create
   ${git} add -f include/mx/gen-version.h

   #
   echo 'nail.1: expanding MKREL'
   < nail.1 > nail.1x ${awk} '
      BEGIN { written = 0 }
      /\.\\"--MKREL-START--/, /\.\\"--MKREL-END--/ {
         if (written++ != 0)
            next
         print ".\\\"--MKREL-START--"
         print ".\\\"@ '"${UPROGRAM}"' v'"${REL}"' / '"${DATE_ISO}"'"
         print ".Dd '"${DATE_MAN}"'"
         print ".ds VV \\\\%v'"${REL}"'"
         print ".\\\"--MKREL-END--"
         next
      }
      {print}
   ' &&
   ${mv} -f nail.1x nail.1
   ${git} add nail.1

   #
   if [ -z "${grappa}" ] && command -v ${roff} >/dev/null 2>&1; then
      echo 'NEWS: updating anchors'
      < nail.1 ${SHELL} mk/mdocmx.sh |
         MDOCMX_ENABLE=1 ${roff} -U -Tutf8 -mdoc \
            -dmx-anchor-dump=/tmp/anchors -dmx-toc-force=tree >/dev/null
      ${SHELL} mk/make-news-anchors.sh /tmp/anchors < NEWS > NEWSx
      ${mv} -f NEWSx NEWS
      ${git} add NEWS
   fi

   #
   echo 'nail.rc: expanding MKREL'
   < nail.rc > nail.rcx ${awk} '
      BEGIN { written = 0 }
      /^#--MKREL-START--/, /^#--MKREL-END--/ {
         if (written++ != 0)
            next
         print "#--MKREL-START--"
         print "#@ '"${UPROGRAM}"' v'"${REL}"' / '"${DATE_ISO}"'"
         print "#--MKREL-END--"
         next
      }
      {print}
   ' &&
   ${mv} -f nail.rcx nail.rc
   ${git} add nail.rc

   #
   ${make} d-cmd-tab && ${git} add -f src/mx/gen-cmd-tab.h
   ${make} d-cs-ctype && ${git} add -f src/su/gen-cs-ctype.h
   if [ -n "${have_perl}" ]; then
      ${make} d-okeys && ${git} add -f src/mx/gen-okeys.h
      ${make} d-tcaps && ${git} add -f src/mx/gen-tcaps.h
      ${make} d-errors && ${git} add -f src/su/gen-errors.h
   fi
}

update_release_hook() {
   #
   echo 'nail.1: stripping MKREL etc.'
   ${sed} -e '/^\.\\"--MKREL-START--/d' \
      -e '/^\.\\"--MKREL-END--/d' \
      -e '/--BEGINSTRIP--/,$ {' \
         -e '/^\.[ 	]*$/d' -e '/^\.[ 	]*\\"/d' \
      -e '}' \
      -e '/^\.$/d' \
      < nail.1 > nail.1x
   ${mv} -f nail.1x nail.1
   ${SHELL} mk/mdocmx.sh < nail.1 > nail.1x
   ${mv} -f nail.1x nail.1
   # And generate the HTML manual, while here
   if [ -z "${grappa}" ] && command -v ${roff} >/dev/null 2>&1; then
      echo 'nail.1: creating HTML manual'
      < nail.1 MDOCMX_ENABLE=1 ${roff} -Thtml -mdoc > /tmp/nail-manual.html
   fi
   ${git} add nail.1

   #
   echo 'nail.rc: stripping MKREL etc.'
   ${sed} -e '/^#--MKREL-START--/d' -e '/^#--MKREL-END--/d' \
      < nail.rc > nail.rcx
   ${mv} -f nail.rcx nail.rc
   ${git} add nail.rc

   ${SHELL} mk/su-make-strip-cxx.sh

   ${make} d-cmd-tab-nv && ${git} add -f src/mx/gen-cmd-tab.h
   ${make} d-cs-ctype-nv && ${git} add -f src/su/gen-cs-ctype.h
   if [ -n "${have_perl}" ]; then
      ${make} d-okeys-nv && ${git} add -f src/mx/gen-okeys.h
      ${make} d-tcaps-nv && ${git} add -f src/mx/gen-tcaps.h
      ${make} d-errors-nv && ${git} add -f src/su/gen-errors.h
      perl mk/su-doc-strip.pl include/su/*.h && ${git} add include/su
   fi

   # Solaris /bin/sh may expand ^ things
   fs=`${git} grep -l '^su_USECASE_MX_DISABLED'`
   ${git} rm -f .gitignore .mailmap TODO \
      \
      mk/make-news-anchors.sh mk/make-release.* \
      \
      mk/mdocmx.sh \
      \
      mk/su-doc-strip.pl mk/su-doxygen.rc mk/su-make-cs-ctype.sh \
      \
      ${fs}
}

test_release_hook() {
   ${git} archive --format=tar --prefix=.xxx/ ${1} |
      (
         cd "${TMPDIR}" || exit 1
         trap "${rm} -rf \"${TMPDIR}\"/.xxx" EXIT
         ${tar} -x -f - || exit 2
         cd .xxx || exit 3
         echo '!!! Test build !!!'
         make tangerine DESTDIR=.yyy || exit 4
      )
   return ${?}
}

. .git/make-release.inc

# s-sh-mode
