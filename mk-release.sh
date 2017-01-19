#!/bin/sh -
#@ mk-release.sh: simple somewhat generic release builder

# In order to be able to remove the release scripts from the release tarball,
# we must delete them, which some shells may not like while they are running.
# So be safe and move instances temporarily to .git/, the .inc will remove them
if [ "`basename \`pwd\``" != .git ]; then
   cp mk-release.* .git/
   cd .git
   exec sh mk-release.sh
fi
cd ..

## Variables

: ${PROGRAM:=s-nail}
: ${UPROGRAM:=S-nail}
: ${MANUAL:=code-nail.html}

: ${UPLOAD:=steffen@sdaoden.eu:/var/www/localhost/downloads}

# Mail
: ${MAILX:=s-nail -Snofollowup-to -Ssmime-sign}
: ${ACCOUNT:=ich}
: ${MAILBCC:=mailx-announce-bcc}
: ${MAILTO:=mailx-announce}

## Hooks

update_stable_hook() {
   if [ -f nail.1 ]; then
      < nail.1 > nail.1x awk '
         BEGIN { written = 0 }
         /\.\\"--MKREL-START--/, /\.\\"--MKREL-END--/ {
            if (written++ != 0)
               next
            print ".\\\"--MKREL-START--"
            print ".\\\"@ '"${UPROGRAM}"'(1): v'"${REL}"' / '"${DATE_ISO}"'"
            print ".Dd '"${DATE_MAN}"'"
            print ".ds VV \\\\%v'"${REL}"'"
            print ".\\\"--MKREL-END--"
            next
         }
         {print}
      ' &&
      mv -f nail.1x nail.1
      git add nail.1
   fi

   if [ -f nail.rc ]; then
      < nail.rc > nail.rcx awk '
         BEGIN { written = 0 }
         /^#--MKREL-START--/, /^#--MKREL-END--/ {
            if (written++ != 0)
               next
            print "#--MKREL-START--"
            print "#@ '"${UPROGRAM}"'(1): v'"${REL}"' / '"${DATE_ISO}"'"
            print "#--MKREL-END--"
            next
         }
         {print}
      ' &&
      mv -f nail.rcx nail.rc
      git add nail.rc
   fi

   [ -f ./mk-okey-map.pl ] && ./mk-okey-map.pl && git add okeys.h
   [ -f ./mk-tcap-map.pl ] && ./mk-tcap-map.pl && git add tcaps.h
}

update_release_hook() {
   if [ -f nail.1 ]; then
      sed -E -e '/^\.\\"--MKREL-(START|END)--/d' \
         -e '/--BEGINSTRIP--/,$ {' \
            -e '/^\.[[:space:]]*$/d' -e '/^\.[[:space:]]*\\"/d' \
         -e '}' \
         -e '/^\.$/d' \
         < nail.1 > nail.1x
      mv -f nail.1x nail.1
      if command -v mdocmx.sh >/dev/null 2>&1; then
         mdocmx.sh < nail.1 > nail.1x
         mv -f nail.1x nail.1
      fi
      git add nail.1
   fi

   if [ -f nail.rc ]; then
      sed -Ee '/^#--MKREL-(START|END)--/d' < nail.rc > nail.rcx
      mv -f nail.rcx nail.rc
      git add nail.rc
   fi

   [ -f ./mk-okey-map.pl ] && ./mk-okey-map.pl noverbose && git add okeys.h
   [ -f ./mk-tcap-map.pl ] && ./mk-tcap-map.pl noverbose && git add tcaps.h
}

. ./mk-release.inc

# s-sh-mode