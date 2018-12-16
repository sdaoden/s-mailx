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

: ${UPLOAD:=steffen@sdaoden.eu:/var/www/localhost/downloads}

: ${MAILX:=s-nail -Snofollowup-to -Sreply-to=mailx -Ssmime-sign}
: ${ACCOUNT:=ich}
: ${MAILBCC:=mailx-announce-bcc}
: ${MAILTO:=mailx-announce}


have_perl=
if command -v perl >/dev/null 2>&1; then
   have_perl=1
fi

## Hooks

current_version() {
   VERSION=`TOPDIR= grep=${grep} sed=${sed} cmp=${cmp} mv=${mv} \
         ${make} -f mk/make-config.in _echo-version`
}

update_stable_hook() {
   [ -n "${grappa}${have_perl}" ] || exit 86

   #
   echo 'gen-version.h: update'
   TOPDIR= grep=${grep} sed=${sed} cmp=${cmp} mv=${mv} \
      ${make} -f mk/make-config.in _update-version
   ${git} add include/mx/gen-version.h

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
   if [ -z "${grappa}" ] && command -v groff >/dev/null 2>&1; then
      echo 'NEWS: updating anchors'
      < nail.1 ${SHELL} mk/mdocmx.sh |
         MDOCMX_ENABLE=1 groff -U -Tutf8 -mdoc \
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
   if [ -n "${have_perl}" ]; then
      ${make} d-okeys && ${git} add src/mx/gen-okeys.h
      ${make} d-tcaps && ${git} add src/mx/gen-tcaps.h
      ${make} d-errors && ${git} add src/su/gen-errors.h
      ${make} d-cs-ctype && ${git} add src/su/gen-cs-ctype.h
   fi
}

update_release_hook() {
   #
   echo 'nail.1: stripping MKREL etc.'
   ${sed} -E -e '/^\.\\"--MKREL-(START|END)--/d' \
      -e '/--BEGINSTRIP--/,$ {' \
         -e '/^\.[[:space:]]*$/d' -e '/^\.[[:space:]]*\\"/d' \
      -e '}' \
      -e '/^\.$/d' \
      < nail.1 > nail.1x
   ${mv} -f nail.1x nail.1
   ${SHELL} mk/mdocmx.sh < nail.1 > nail.1x
   ${mv} -f nail.1x nail.1
   # And generate the HTML manual, while here
   if [ -z "${grappa}" ] && command -v groff >/dev/null 2>&1; then
      < nail.1 MDOCMX_ENABLE=1 groff -Thtml -mdoc > /tmp/nail-manual.html
   fi
   ${git} add nail.1

   #
   echo 'nail.rc: stripping MKREL etc.'
   ${sed} -Ee '/^#--MKREL-(START|END)--/d' < nail.rc > nail.rcx
   ${mv} -f nail.rcx nail.rc
   ${git} add nail.rc

   if [ -n "${have_perl}" ]; then
      ${make} d-okeys-nv && ${git} add src/mx/gen-okeys.h
      ${make} d-tcaps-nv && ${git} add src/mx/gen-tcaps.h
      ${make} d-errors-nv && ${git} add src/su/gen-errors.h
      ${make} d-cs-ctype-nv && ${git} add src/su/gen-cs-ctype.h
   fi

   ${git} rm -f .gitignore .mailmap TODO \
      mk/make-release.* mk/make-news-anchors.sh mk/mdocmx.sh \
      `${git} grep -l ^su_USECASE_MX_DISABLED`
}

. .git/make-release.inc

# s-sh-mode
