#@ Include file for the make-release.sh generic release builder.
#@ It also needs two hooks: update_stable_hook(), update_release_hook(),
#@ which need to "git add" what they have modified.
#@ The "grappa" mode needs a current_version() hook, which has to set $VERSION
#@ to the current program version, expected as MAJOR.MINOR.UPDATE[-whatever]

: ${PROGRAM:?"Need \$PROGRAM"}
: ${UPROGRAM:?"Need \$UPROGRAM"}
# For announcement only.
: ${MANUAL:?"May need \$MANUAL for announcement references"}

# When we upload balls only.
: ${UPLOAD:?"Need \$UPLOAD URL for scp(1)"}

# For announcement mail only.
: ${MAILX:=mailx}
: ${ACCOUNT:?"May need mailx(1) -A \$ACCOUNT"}
: ${MAILTO:?"May need \$MAILTO for announcement"}
: ${MAILBCC:?"May need \$MAILBCC for announcement"}

# Program stuff
: ${awk:=awk}
: ${cat:=cat}
: ${cmp:=cmp}
: ${date:=date}
: ${git:=git}
: ${grep:=grep}
: ${make:=make}
: ${mv:=mv}
: ${SHELL:=sh}
: ${sed:=sed}

# non-grappa only
: ${gzip:=gzip}
: ${openssl:=openssl}
: ${gpg:=gpg}
: ${rm:=rm}
: ${sftp:=sftp}
: ${tar:=tar}
: ${xz:=xz}

##  --  >8  --  8<  --  ##

ORIG_LC_ALL=${LC_ALL} LC_ALL=C
export LC_ALL

DATE_MAN=`${date} -u +'%B %d, %Y'`
DATE_ISO=`${date} -u +%Y-%m-%d`

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

ref_status() {
   headref="`${git} rev-parse --verify HEAD`"
   brref=
   for i in `${git} rev-parse --branches=stable master^{commit} \
         2>/dev/null`; do
      if [ ${headref} = ${i} ]; then
         brref=${headref}
         break
      fi
   done
   if [ -z "${brref}" ]; then
      echo >&2 'Not on the [master] or a [stable/*] branch'
      exit 1
   fi
   if [ "`${git} status --porcelain --ignored |
         ${awk} 'BEGIN{no=0}{++no}END{print no}'`" -ne 0 ]; then
      echo >&2 'Directory not clean, see git status --ignored'
      exit 2
   fi
   #brname="`git branch | sed -e '/^* /b X' -e d -e :X -e 's/^* //'`"
   brname=`${git} symbolic-ref --short HEAD`
}

release_version() {
   vmaj=`{ echo ${VERSION}; } | ${sed} -e 's/^\([^.]\{1,\}\).*/\1/'`
   vmin=`{ echo ${VERSION}; } |
      ${sed} -e 's/^[^.]\{1,\}\.\([^.]\{1,\}\).*/\1/'`
   [ ${vmin} = ${VERSION} ] && VERSION=${VERSION}.0 vmin=0
   vupd=`{ echo ${VERSION}; } |
         ${sed} -e 's/^[^.]\{1,\}\.[^.]\{1,\}\.\([^.-]\{1,\}\).*/\1/'`
   [ ${vupd} = ${VERSION} ] && VERSION=${VERSION}.0 vupd=0
   REL=${VERSION}
   export VERSION
   if yesno 'Is '${PROGRAM}' <v'${REL}'> correct?'; then :; else
      echo >&2 'Bailing out'
      exit 3
   fi
}

release_brcheck() {
   stblbrname=stable/v${vmaj}.${vmin} need_stblbrname=
   brref=`${git} rev-parse --verify ${stblbrname} 2>/dev/null`
   if [ -z "${brref}" ]; then
      if yesno 'Create new branch '"${stblbrname}"' after release tag'; then
         need_stblbrname=1
      fi
   elif [ ${brref} != ${headref} ] || [ ${brname} != ${stblbrname} ]; then
      echo >&2 "For ${REL} we should be on ${stblbrname}, not ${brname}"
      echo >&2 'Bailing out'
      exit 4
   fi

   relbrname=release/v${VERSION}
   brref=`${git} rev-parse --verify ${relbrname} 2>/dev/null`
   if [ -z "${brref}" ]; then :; else
      echo >&2 "The ${relbrname} already exists"
      echo >&2 'Bailing out'
      exit 5
   fi
}

release_symname() {
   RELSYM=
   stblmsg= relmsg=
   if yesno 'Shall '${PROGRAM}' v'${REL}' have a symbolic name?'; then
      printf '  ..and it shall be known as: '
      read RELSYM
      if yesno 'Is '"${RELSYM}"' correct?'; then :; else
         echo >&2 'Bailing out'
         exit 3
      fi
      stblmsg="Bump ${UPROGRAM} v${REL} (\"${RELSYM}\"), ${DATE_ISO}"
      relmsg="Bump ${UPROGRAM} v${REL}.ar (\"${RELSYM}\"), ${DATE_ISO}"
      RELSYM=" (\"${RELSYM}\")"
   else
      stblmsg="Bump ${UPROGRAM} v${REL}, ${DATE_ISO}"
      relmsg="Bump ${UPROGRAM} v${REL}.ar, ${DATE_ISO}"
   fi
}

update_stable_branch() {
   LC_ALL=${ORIG_LC_ALL} ${git} commit -S -n -m "${stblmsg}"
   LC_ALL=${ORIG_LC_ALL} ${git} tag -s -f -m "${stblmsg}" v${REL}

   if [ -n "${need_stblbrname}" ]; then
      ${git} checkout -b ${stblbrname}
   fi
   # Normally done in post-commit hook, but not once initially created
   if yesno 'Shall i update stable/latest "symlink"?'; then
      ${git} update-ref refs/heads/stable/latest ${stblbrname}
   fi
   if yesno 'Shall i update stable/stable "symlink"?'; then
      ${git} update-ref refs/heads/stable/stable ${stblbrname}
   fi
}

create_release_branch() {
   if yesno 'Create release/ branch?'; then
      ${git} checkout -b ${relbrname}

      echo 'Updating files: calling update_release_hook'
      update_release_hook

      LC_ALL=${ORIG_LC_ALL} ${git} commit -S -n -m "${relmsg}"
      LC_ALL=${ORIG_LC_ALL} ${git} tag -s -f -m "${relmsg}" v${REL}.ar

      if yesno 'Shall i update release/latest "symlink"?'; then
         ${git} update-ref refs/heads/release/latest ${relbrname}
      fi
      if yesno 'Shall i update release/stable "symlink"?'; then
         ${git} update-ref refs/heads/release/stable ${relbrname}
      fi
   else
      relbrname=${stblbrname}
   fi
}

check_timeline_branch() {
   if [ ${relbrname} != ${stblbrname} ] &&
         `${git} rev-parse --verify timeline^{commit} >/dev/null 2>&1` &&
         yesno 'Shall i update [timeline]?'; then
      ${git} checkout timeline
      ${git} rm -rf '*'
      ${git} archive --format=tar "v${REL}.ar" | ${tar} -x -f -
      ${git} add .
      LC_ALL=${ORIG_LC_ALL} ${git} commit -S -n -m "${relmsg}"
   fi
}

repo_push() {
   [ ${relbrname} != ${stblbrname} ] && ${git} checkout ${stblbrname}
   ${git} log --no-walk --decorate --oneline --branches --remotes
   yesno 'Push git(1) repo?' && ${git} push
}

big_balls() {
   if [ ${relbrname} != ${stblbrname} ] && yesno 'Create tarballs?'; then
      bigballs=y
      (
      umask 0022

      # Repack with standard tar(1) to avoid new-style headers
      ${git} archive --format=tar --prefix="${PROGRAM}-${REL}/" v${REL}.ar |
         ( cd "${TMPDIR}" && ${tar} -x -f - )
      cd "${TMPDIR}"

      ${tar} -c -f "${PROGRAM}-${REL}.tar" "${PROGRAM}-${REL}"
      < "${PROGRAM}-${REL}.tar" ${xz} -e -C sha256 > "${PROGRAM}-${REL}.tar.xz"
      < "${PROGRAM}-${REL}.tar" ${gzip} > "${PROGRAM}-${REL}.tar.gz"
      ${rm} "${PROGRAM}-${REL}.tar"

      printf '' > "${PROGRAM}-${REL}.cksum"
      ${openssl} sha1 "${PROGRAM}-${REL}.tar.xz" >> "${PROGRAM}-${REL}.cksum"
      ${openssl} sha256 "${PROGRAM}-${REL}.tar.xz" >> "${PROGRAM}-${REL}.cksum"
      ${openssl} sha512 "${PROGRAM}-${REL}.tar.xz" >> "${PROGRAM}-${REL}.cksum"
      ${openssl} sha1 "${PROGRAM}-${REL}.tar.gz" >> "${PROGRAM}-${REL}.cksum"
      ${openssl} sha256 "${PROGRAM}-${REL}.tar.gz" >> "${PROGRAM}-${REL}.cksum"
      ${openssl} sha512 "${PROGRAM}-${REL}.tar.gz" >> "${PROGRAM}-${REL}.cksum"

      echo >> "${PROGRAM}-${REL}.cksum"
      ${gpg} --detach-sign --armor "${PROGRAM}-${REL}.tar.xz"
      ${cat} "${PROGRAM}-${REL}.tar.xz.asc" >> "${PROGRAM}-${REL}.cksum"
      ${gpg} --detach-sign --armor "${PROGRAM}-${REL}.tar.gz"
      ${cat} "${PROGRAM}-${REL}.tar.gz.asc" >> "${PROGRAM}-${REL}.cksum"
      )
   else
      bigballs=
   fi
}

announcement_prepare() {
   anntxt=
   if yesno 'Prepare announcement?'; then :; else
      return
   fi
   anntxt=y

   if `${git} cat-file -e ${relbr}:NEWS 2>/dev/null`; then
      ${git} show ${relbr}:NEWS > "${TMPDIR}/.${PROGRAM}-${REL}.news"
   else
      printf '' > "${TMPDIR}/.${PROGRAM}-${REL}.news"
   fi

   { echo "${relmsg}"; echo; } > "${TMPDIR}/${PROGRAM}-${REL}.txt"
   if [ -f .git/make-release.txt ]; then
      # For the checksums
      if [ -n "${bigballs}" ] && [ -f "${TMPDIR}/${PROGRAM}-${REL}.cksum" ]
      then
         cks=`< "${TMPDIR}/${PROGRAM}-${REL}.cksum" \
               ${sed} -e 's/ //' -e '/^$/,$d'`
         < "${TMPDIR}/${PROGRAM}-${REL}.cksum" ${sed} '1,/^$/d' \
            > "${TMPDIR}/.${PROGRAM}-${REL}.sigs"
         < .git/make-release.txt ${awk} \
               -v INS="${cks}" -v SIGS="${TMPDIR}/.${PROGRAM}-${REL}.sigs" \
               -v NEWS="${TMPDIR}/.${PROGRAM}-${REL}.news" '
            /-----CHECKSUMS-----/{
               atop = split(INS, a)
               fn = ""
               for(i = 1; i <= atop; ++i){
                  match(a[i], /(\(.+\))/)
                  tfn = substr(a[i], RSTART + 1, RLENGTH - 2)
                  tpre = substr(a[i], 1, RSTART - 1)
                  tsuf = substr(a[i], RSTART + RLENGTH + 1)
                  if(fn == "" || fn != tfn)
                     printf "%s:\n", (fn = tfn)
                  printf "  %6s %s\n", tpre, tsuf
               }
               next
            }
            /-----SIGNATURES-----/{
               while(getline sl < SIGS)
                  print sl
               next
            }
            /-----NEWS-----/{
               while(getline sl < NEWS)
                  print sl
               next
            }
            {print}
         ' >> "${TMPDIR}/${PROGRAM}-${REL}.txt"
         ${rm} -f "${TMPDIR}/.${PROGRAM}-${REL}.sigs"
      else
         < .git/make-release.txt ${awk} \
               -v NEWS="${TMPDIR}/.${PROGRAM}-${REL}.news" '
            /-----NEWS-----/{
               while(getline sl < NEWS)
                  print sl
               next
            }
            {print}
         ' >> "${TMPDIR}/${PROGRAM}-${REL}.txt"
      fi
   elif [ -f "${TMPDIR}/.${PROGRAM}-${REL}.news" ]; then
      ${cat} "${TMPDIR}/.${PROGRAM}-${REL}.news" >> \
         "${TMPDIR}/${PROGRAM}-${REL}.txt"
   fi

   ${rm} -f "${TMPDIR}/.${PROGRAM}-${REL}.news"

   LC_ALL=${ORIG_LC_ALL} ${EDITOR} "${TMPDIR}/${PROGRAM}-${REL}.txt"

   # HTML convert ready for S-Web42
   APO=\'
   < "${TMPDIR}/${PROGRAM}-${REL}.txt" ${awk} -v manual="${MANUAL}" '
   BEGIN{
      hot = 0
      print "<?begin?><?mode icewatsm?><pre>"
   }
   function strips(){
      gsub("&", "\\&amp;")
      gsub("<", "\\&lt;")
      gsub(">", "\\&gt;")
   }
   function urls(){
      any = 0
      res = ""
      s = $0

      while(match(s, /(\\?https?\??:\/\/[^ ]*)/)){
         pre = substr(s, 1, RSTART - 1)
         mat = substr(s, RSTART, RLENGTH)
         s = substr(s, RSTART + RLENGTH)
         if("\\" == substr(mat, 1, 1))
            mat = substr(mat, 2)
         else{
            xt = 0
            if(match(mat, /^https\?/))
               mat = "https" substr(xt = mat, RSTART + 6)
            if(match(mat, /sdaoden\.eu/))
               mat = "<?lref" (xt ? "t " : " ") mat (xt ? "<>" xt : "") "?>"
            else
               mat = "<?href" (xt ? "t " : " ") mat (xt ? "<>" xt : "") "?>"
         }
         res = res pre mat
         any = 1
      }
      if(any && length(s))
         res = res s
      $0 = any ? res : s
   }
   /^[[:space:]]*s-.*-mode[[:space:]]*$/{
      exit 0
   }
   /^(NOTES|ChangeLog)/{
      hot = 1
      strips()
      print
      next
   }
   /^(Appendix|git\(1\) shortlog)/{
      hot = -1
      strips()
      print
      next
   }
   {
      strips()
      urls()
      if(hot <= 0){
         print
         next
      }
      any = 0
      res = ""
      s = $0
      # Create S-Web42 local references for the possible anchors:
      #     *XY*# / $XY# / -XY# / `XY${APO}# / `~XY${APO}# / "XY"#
      # (where the mdocmx(7) anchor follows the number sign).
      # Ideally the anchors have been automatically expanded by
      # make-news-anchors.sh before.
      while(match(s,
            /(^|\(|[[:space:]]+)("[^"]+"|\*[^\*]+\*|`[^'${APO}']+'${APO}'|[-~][-#\/:_.[:alnum:]]+|\$[_[:alnum:]]+)#[0-9]+/))
      {
         pre = (RSTART > 1) ? substr(s, 1, RSTART - 1) : ""
         mat = substr(s, RSTART, RLENGTH)
         s = substr(s, RSTART + RLENGTH)

         # Unfortunately groups are not supported
         if(match(mat, /^(\(|[[:space:]]+)/) != 0 && RLENGTH > 0){
            pre = pre substr(mat, 1, RLENGTH)
            mat = substr(mat, RSTART + RLENGTH)
         }

         match(mat, /#[0-9]+/)
         targ = substr(mat, RSTART + 1, RLENGTH)
         mat = substr(mat, 1, RSTART - 1)
         res = res pre "<?lreft " manual "#" targ "<>" mat "?>"
         any = 1
      }
      if(any && length(s))
         res = res s
      print any ? res : s
   }
   END{
      print "</pre><?end?>"
   }
   ' > "${TMPDIR}/.${PROGRAM}-ann.html"
}

upload() (
   if [ -n "${bigballs}" ] && yesno 'Upload archives'; then :; else
      return
   fi
   cd "${TMPDIR}"

   {
      echo "-put ${PROGRAM}-${REL}.tar.xz"
      echo "-rm ${PROGRAM}-latest.tar.xz"
      echo "-ln -s ${PROGRAM}-${REL}.tar.xz ${PROGRAM}-latest.tar.xz"

      echo "-put ${PROGRAM}-${REL}.tar.xz.asc"
      echo "-rm ${PROGRAM}-latest.tar.xz.asc"
      echo "-ln -s ${PROGRAM}-${REL}.tar.xz.asc ${PROGRAM}-latest.tar.xz.asc"

      echo "-put ${PROGRAM}-${REL}.tar.gz"
      echo "-rm ${PROGRAM}-latest.tar.gz"
      echo "-ln -s ${PROGRAM}-${REL}.tar.gz ${PROGRAM}-latest.tar.gz"

      echo "-put ${PROGRAM}-${REL}.tar.gz.asc"
      echo "-rm ${PROGRAM}-latest.tar.gz.asc"
      echo "-ln -s ${PROGRAM}-${REL}.tar.gz.asc ${PROGRAM}-latest.tar.gz.asc"

      if [ -n "${anntxt}" ]; then
         echo "-put ${PROGRAM}-${REL}.txt"
         echo "-rm ${PROGRAM}-latest.txt"
         echo "-ln -s ${PROGRAM}-${REL}.txt ${PROGRAM}-latest.txt"
      fi

      echo "-chmod 0644 ${PROGRAM}-${REL}.*"
   } |
   ${sftp} -b - ${UPLOAD}
)

announcement_send() {
   if [ -n "${anntxt}" ] && yesno 'Send announcement mail?'; then
      LC_ALL=${ORIG_LC_ALL} ${MAILX} -A ${ACCOUNT} \
         -s "[ANN]ounce of ${UPROGRAM} v${REL}${RELSYM}" \
         -q "${TMPDIR}/${PROGRAM}-${REL}.txt" \
         -b ${MAILBCC} ${MAILTO}
   fi
}

create_grappa_env() {
   echo 'Updating files: calling update_release_hook'
   update_release_hook
   echo 'E allora io quasi quasi prendo il treno'
}

grappa=
if [ ${#} -ne 0 ]; then
   if [ ${#} != 2 ] || [ "${1}" != grappa ] || [ -z "${2}" ]; then
      echo >&2 'You have a hell of a lot to learn about Rock'"'"'n Roll'
      exit 55
   fi
   grappa=${2}
fi

ref_status
echo 'Preparing a release on commit '"${headref}"
if [ -z "${grappa}" ]; then
   printf '  The HEAD is %s\nName of release tag: ' "${brname}"
   read REL
   VERSION=${REL}
   release_version
   release_brcheck
   release_symname
else
   echo 'Grappa to be brought from '"${brname}"' to '"${grappa}"
   current_version
   printf 'Program version is %s, packager release addition shall be: ' \
      "${VERSION}"
   read REL
   VERSION="${VERSION}-${REL}"
   release_version

   i=
   if ${git} rev-parse --verify ${grappa} >/dev/null 2>/dev/null; then :; else
      i=-B
   fi
   ${git} checkout ${i} ${grappa}
   ${git} rm -f '*'
   ${git} archive --format=tar ${headref} | ${tar} -x -f -
   ${git} add .
fi

echo 'Updating files: calling update_stable_hook'
update_stable_hook

if [ -z "${grappa}" ]; then
   update_stable_branch
   create_release_branch
   check_timeline_branch
   repo_push
   big_balls
   announcement_prepare
   upload
   announcement_send
else
   create_grappa_env
fi


# Finally remove the temporary instances than ran this
${rm} -f .git/make-release.*
echo 'Done'
exit
# s-sh-mode
