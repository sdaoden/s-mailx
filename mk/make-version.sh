#!/bin/sh -
#@ Generate new include/mx/gen-version.h (via environment settings).
#@ MUST work with "TOPDIR= awk=awk grep=grep sed=sed cmp=cmp mv=mv".
#
# Public Domain

LC_ALL=C

query() {
   VERSION="`< ${CWDDIR}include/mx/gen-version.h ${sed} \
         -e '/ mx_VERSION /b X' -e d -e ':X' \
         -e 's/[^\"]*\"v\([^\"]\{1,\}\)\"/\1/'`"
   echo $VERSION
}

c__gitver() {
   [ -n "${TOPDIR}" ] && cd "${TOPDIR}"
   if [ -d .git ] && command -v git >/dev/null 2>&1; then
      git describe --tags
   fi
}

c__isdirty() {
   [ -n "${TOPDIR}" ] && cd "${TOPDIR}"
   _id=`git status --porcelain | ${awk} '
      BEGIN {n=0}
      /gen-version\.h/ {next}
      /^\?\?/ {next}
      {++n}
      END {print n}
      '`
   [ "${_id}" != 0 ]
}

create() {
   if [ -z "${VERSION}" ]; then
      VERSION=`c__gitver`
      if [ -n "${VERSION}" ]; then
         VERSION="`echo ${VERSION} | ${sed} -e 's/^v\{0,1\}\(.*\)/\1/'`"
         c__isdirty && VERSION="${VERSION}-dirty"
      else
         query | ${grep} -q -F dirty || VERSION="${VERSION}-dirty"
      fi
   fi

   vmaj="`echo \"${VERSION}\" | ${sed} -e 's/^\([^.]\{1,\}\).*/\1/'`"
   vmin="`echo \"${VERSION}\" |
         ${sed} -e 's/^[^.]\{1,\}\.\([^.]\{1,\}\).*/\1/'`"
   [ "${vmin}" = "${VERSION}" ] && VERSION="${VERSION}.0" vmin=0
   vupd="`echo \"${VERSION}\" |
         ${sed} -e 's/^[^.]\{1,\}\.[^.]\{1,\}\.\([^.-]\{1,\}\).*/\1/'`"
   [ "${vupd}" = "${VERSION}" ] && VERSION="${VERSION}.0" vupd=0

   trap "${rm} -f ./version.tmp" 0 1 2 15

   printf > ./version.tmp "#define mx_VERSION \"v${VERSION}\"\n"
   printf >> ./version.tmp "#define mx_VERSION_DATE \"%s\"\n" \
      "`date -u +'%Y-%m-%d'`"
   printf >> ./version.tmp "#define mx_VERSION_MAJOR \"${vmaj}\"\n"
   printf >> ./version.tmp "#define mx_VERSION_MINOR \"${vmin}\"\n"
   printf >> ./version.tmp "#define mx_VERSION_UPDATE \"${vupd}\"\n"
   printf >> ./version.tmp "#define mx_VERSION_HEXNUM \"0x%02X%03X%03X\"\n" \
      "${vmaj}" "${vmin}" "${vupd}"
   ${cmp} ./version.tmp ${CWDDIR}include/mx/gen-version.h >/dev/null 2>&1 &&
      exit
   ${mv} ./version.tmp ${CWDDIR}include/mx/gen-version.h

   trap : 0 1 2 15
}

syno() {
   echo >&2 'Synopsis: make-version.sh create|query'
   exit 1
}

[ $# -ne 1 ] && syno
[ "$1" = create ] && { create; query; exit 0; }
[ "$1" = query ] && { query; exit 0; }
syno

# s-sh-mode
