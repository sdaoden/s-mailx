#!/bin/sh -
#@ Install ourselves, and generate uninstall script.
#
# Public Domain

LC_ALL=C

__mkdir() {
   _dir="${DESTDIR}${1}"
   if [ -d "${_dir}" ]; then :; else
      ${mkdir} -m 0755 -p "${_dir}"
   fi
}

__copyfile() {
   _mode=${1} _src=${2} _xdst=${3} _dst="${DESTDIR}${3}"
   echo "rm -f \"\${DESTDIR}${_xdst}\"" >> \
      "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
   ${cp} -f "${_src}" "${_dst}"
   ${chmod} ${_mode} "${_dst}"
}

__copychownfile() {
   _mode=${1} _ident=${2} _src=${3} _xdst=${4} _dst="${DESTDIR}${4}"
   echo "rm -f \"\${DESTDIR}${_xdst}\"" >> \
      "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
   ${cp} -f "${_src}" "${_dst}"
   ${chown} ${_ident} "${_dst}" || true
   ${chmod} ${_mode} "${_dst}"
}

__stripfile() {
   _file=${1}
   if [ "${OPT_DEBUG}" != 0 ]; then :;
   elif [ -n "${strip}" ]; then
      ${strip} "${_file}"
   fi
}

cd "${CWDDIR}" || exit 11

echo '#!/bin/sh -' > "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo '#@ Uninstall script for '"${VAL_UAGENT}" >> \
   "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo 'DESTDIR="'${DESTDIR}'"' >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo 'DESTDIR=' >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"

[ -n "${DESTDIR}" ] && __mkdir ''
__mkdir "${VAL_BINDIR}"
__mkdir "${VAL_MANDIR}/man1"
__mkdir "${VAL_SYSCONFDIR}"

__stripfile "${OBJDIR}"/"${VAL_UAGENT}"
__copyfile 0555 "${OBJDIR}"/"${VAL_UAGENT}" "${VAL_BINDIR}"/"${VAL_UAGENT}"
__copyfile 0444 "${OBJDIR}"/uman.1 "${VAL_MANDIR}"/man1/"${VAL_UAGENT}".1
if [ -f "${DESTDIR}${VAL_SYSCONFDIR}/${VAL_SYSCONFRC}" ]; then :; else
   __copyfile 0444 "${OBJDIR}"/urc.rc "${VAL_SYSCONFDIR}/${VAL_SYSCONFRC}"
fi

if [ "${OPT_DOTLOCK}" != 0 ]; then
   __mkdir "${VAL_LIBEXECDIR}"

   __stripfile "${OBJDIR}"/"${VAL_PS_DOTLOCK}"
   m='o=rx' o=
   #if [ -n "${_____PRIVSEP_GROUP}" ]; then
   #   m="g=rxs,${m}" o=":${VAL_PRIVSEP_GROUP}"
   #else
      m="g=rx,${m}"
   #fi
   if [ -n "${VAL_PS_DOTLOCK_USER}" ]; then
      m="u=rxs,${m}" o="${VAL_PS_DOTLOCK_USER}${o}"
   else
      m="u=rx,${m}"
   fi;
   __copychownfile "${m}" "${o}" \
      "${OBJDIR}"/"${VAL_PS_DOTLOCK}" "${VAL_LIBEXECDIR}/${VAL_PS_DOTLOCK}"
fi;

if [ -z "${DESTDIR}" ]; then
   __copyfile 0555 "${OBJDIR}/${VAL_UAGENT}-uninstall.sh" \
      "${VAL_BINDIR}/${VAL_UAGENT}-uninstall.sh"
else
   echo "rm -f \"\${DESTDIR}${VAL_BINDIR}/${VAL_UAGENT}\"-uninstall.sh" >> \
      "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
fi

# s-sh-mode
