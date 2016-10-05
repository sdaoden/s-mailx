#@ Include file for the mk-release.sh generic release builder.
#@ It also needs two hooks: update_stable_hook(), update_release_hook(),
#@ which need to "git add" what they have modified.

[ -z "${PROGRAM}" ] && { echo >&2 'Need $PROGRAM'; exit 1; }
[ -z "${UPROGRAM}" ] && { echo >&2 'Need $UPROGRAM'; exit 1; }
[ -z "${UPLOAD}" ] && { echo >&2 'Need $UPLOAD URL for scp(1)'; exit 1; }
: ${MAILX:=mailx}
[ -z "${ACCOUNT}" ] && { echo >&2 'Need mailx(1) -A $ACCOUNT'; exit 1; }
[ -z "${MAILTO}" ] && { echo >&2 'Need $MAILTO for announcement'; exit 1; }
[ -z "${MAILBCC}" ] && { echo >&2 'Need $MAILBCC for announcement'; exit 1; }

##  --  >8  --  8<  --  ##

ORIG_LC_ALL=${LC_ALL} LC_ALL=C
export LC_ALL

DATE_MAN=`date -u +'%b %d, %Y'`
DATE_ISO=`date -u +%Y-%m-%d`

yesno() {
   while [ 1 ]; do
      [ ${#} -gt 0 ] && printf '%s ' "${@}"
      printf '[y/n] '
      read i
      case ${i} in
      [Yy]*) return 0;;
      [Nn]*) return 1;;
      *) ;;
      esac
   done
}

headref="`git rev-parse --verify HEAD`"
brref=
for i in `git rev-parse --branches=stable master^{commit}`; do
   if [ ${headref} = ${i} ]; then
      brref=${headref}
      break
   fi
done
if [ -z "${brref}" ]; then
   echo >&2 'Not on the [master] or a [stable/*] branch'
   exit 1
fi
if [ "`git status --porcelain --ignored |
      awk 'BEGIN{no=0}{++no}END{print no}'`" -ne 0 ]; then
   echo >&2 'Directory not clean, see git status --ignored'
   exit 2
fi

echo 'Preparing a release on commit '"${headref}"
#brname="`git branch | sed -e '/^* /b X' -e d -e :X -e 's/^* //'`"
brname=`git symbolic-ref --short HEAD`
echo '  That is '"${brname}"
printf '  Name of release tag: '
read REL
VERSION=${REL}
vmaj=`{ echo ${VERSION}; } | sed -e 's/^\([^.]\{1,\}\).*/\1/'`
vmin=`{ echo ${VERSION}; } | sed -e 's/^[^.]\{1,\}\.\([^.]\{1,\}\).*/\1/'`
[ ${vmin} = ${VERSION} ] && VERSION=${VERSION}.0 vmin=0
vupd=`{ echo ${VERSION}; } |
      sed -e 's/^[^.]\{1,\}\.[^.]\{1,\}\.\([^.-]\{1,\}\).*/\1/'`
[ ${vupd} = ${VERSION} ] && VERSION=${VERSION}.0 vupd=0
REL=${VERSION}
export VERSION
if yesno 'Is '${PROGRAM}' <v'${REL}'> correct?'; then :; else
   echo >&2 'Bailing out'
   exit 3
fi

stblbrname=stable/v${vmaj}.${vmin}
brref=`git rev-parse --verify ${stblbrname} 2>/dev/null`
if [ -z "${brref}" ]; then
   if yesno 'Create and switch to new branch '"${stblbrname}"; then
      git checkout -b ${stblbrname}
   fi
elif [ ${brref} != ${headref} ] || [ ${brname} != ${stblbrname} ]; then
   echo >&2 "For ${REL} we should be on ${stblbrname}, not ${brname}"
   echo >&2 'Bailing out'
   exit 4
fi

relbrname=release/v${VERSION}
brref=`git rev-parse --verify ${relbrname} 2>/dev/null`
if [ -z "${brref}" ]; then :; else
   echo >&2 "The ${relbrname} already exists"
   echo >&2 'Bailing out'
   exit 5
fi

##
echo 'Updating stable/ files to match the release'

if [ -f version.h ] && [ -f mk-mk.in ]; then
   grep=grep sed=sed cmp=cmp mv=mv make -f mk-mk.in _update-version
   git add version.h
fi
update_stable_hook

LC_ALL=${ORIG_LC_ALL} git commit -S -m "Bump ${UPROGRAM} v${REL}, ${DATE_ISO}"
LC_ALL=${ORIG_LC_ALL} git tag -s -f \
   -m "Bump ${UPROGRAM} v${REL}, ${DATE_ISO}" "v${REL}"

##

if yesno 'Create release/ branch and files?'; then
   git checkout -b ${relbrname}

   git rm -f .gitignore .mailmap TODO
   update_release_hook

   LC_ALL=${ORIG_LC_ALL} git commit -S \
      -m "Bump ${UPROGRAM} v${REL}.ar, ${DATE_ISO}"
   LC_ALL=${ORIG_LC_ALL} git tag -s -f \
      -m "Bump ${UPROGRAM} v${REL}.ar, ${DATE_ISO}" "v${REL}.ar"

   if yesno 'Shall i update release/latest "symlink"?'; then
      git update-ref refs/heads/release/latest ${relbrname}
   fi
   if yesno 'Shall i update release/stable "symlink"?'; then
      git update-ref refs/heads/release/stable ${relbrname}
   fi
else
   relbrname=${stblbrname}
fi

##
# [timeline]

if [ ${relbrname} != ${stblbrname} ] &&
      `git rev-parse --verify timeline^{commit} >/dev/null 2>&1` &&
      yesno 'Shall i update [timeline]?'; then
   git checkout timeline
   git rm -rf '*'
   git archive --format=tar "v${REL}.ar" | tar -x -f -
   git add .
   LC_ALL=${ORIG_LC_ALL} git commit -S -m "${UPROGRAM} v${REL}.ar, ${DATE_ISO}"
fi

##
# repo push

[ ${relbrname} != ${stblbrname} ] && git checkout ${stblbrname}
git log --no-walk --decorate --oneline --branches --remotes
yesno 'Push git(1) repo?' && git push

##
# Big balls

if [ ${relbrname} != ${stblbrname} ] && yesno 'Create tarballs?'; then
   (
   # Repack with standard tar(1) to avoid new-style headers
   git archive --format=tar --prefix="${PROGRAM}-${REL}/" "v${REL}.ar" |
      ( cd "${TMPDIR}" && tar -x -f - )
   cd "${TMPDIR}"
   tar -c -f "${PROGRAM}-${REL}.tar" "${PROGRAM}-${REL}"

   openssl md5 "${PROGRAM}-${REL}.tar" > "${PROGRAM}-${REL}.cksum"
   openssl sha1 "${PROGRAM}-${REL}.tar" >> "${PROGRAM}-${REL}.cksum"
   openssl sha256 "${PROGRAM}-${REL}.tar" >> "${PROGRAM}-${REL}.cksum"
   openssl sha512 "${PROGRAM}-${REL}.tar" >> "${PROGRAM}-${REL}.cksum"
   gpg --detach-sign --armor "${PROGRAM}-${REL}.tar"
   cat "${PROGRAM}-${REL}.tar.asc" >> "${PROGRAM}-${REL}.cksum"

   < "${PROGRAM}-${REL}.tar" gzip > "${PROGRAM}-${REL}.tar.gz"
   < "${PROGRAM}-${REL}.tar" xz -e -C sha256 > "${PROGRAM}-${REL}.tar.xz"

   if yesno 'Upload archives'; then
      (
      echo "-put ${PROGRAM}-${REL}.tar"
      echo "-rm ${PROGRAM}-latest.tar"
      echo "-ln ${PROGRAM}-${REL}.tar ${PROGRAM}-latest.tar"

      echo "-put ${PROGRAM}-${REL}.tar.gz"
      echo "-rm ${PROGRAM}-latest.tar.gz"
      echo "-ln ${PROGRAM}-${REL}.tar.gz ${PROGRAM}-latest.tar.gz"

      echo "-put ${PROGRAM}-${REL}.tar.xz"
      echo "-rm ${PROGRAM}-latest.tar.xz"
      echo "-ln ${PROGRAM}-${REL}.tar.xz ${PROGRAM}-latest.tar.xz"

      echo "-put ${PROGRAM}-${REL}.tar.asc"
      echo "-rm ${PROGRAM}-latest.tar.asc"
      echo "-ln ${PROGRAM}-${REL}.tar.asc ${PROGRAM}-latest.tar.asc"

      echo "-chmod 0644 ${PROGRAM}-${REL}.tar*"
      ) |
      sftp -b - ${UPLOAD}
   fi
   )
fi

##
# Announcement

if yesno 'Send announcement mail?'; then
   if `git rev-parse --verify announce^{blob} >/dev/null 2>&1`; then
      git show announce > "${TMPDIR}/${PROGRAM}-${REL}.ann.mail"
   else
      echo > "${TMPDIR}/${PROGRAM}-${REL}.ann.mail"
   fi
   if `git cat-file -e ${relbr}:NEWS 2>/dev/null`; then
      git show ${relbr}:NEWS >> "${TMPDIR}/${PROGRAM}-${REL}.ann.mail"
   fi

   LC_ALL=${ORIG_LC_ALL} ${MAILX} -A ${ACCOUNT} \
      -s "[ANN]ounce of ${UPROGRAM} v${REL}" \
      -q "${TMPDIR}/${PROGRAM}-${REL}.ann.mail" \
      -b ${MAILBCC} ${MAILTO}
fi

# Finally remove the temporary instances than ran this
rm -f .git/mk-release.sh .git/mk-release.inc
echo 'Done'
exit
# s-sh-mode
