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

___docopychownfile() {
	_xstrip=${1} _mode=${2} _ident=${3} _src=${4} _xdst=${5} _dst="${DESTDIR}${5}"

	${cp} -f "${_src}" "${_dst}"

	echo "${rm} -f \"\${DESTDIR}${_xdst}\"" >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"

	if [ "${_xstrip}" = y ]; then
		if [ "${OPT_DEBUG}" != 0 ]; then
			if [ -n "${DEBUG_IN_EXTERNAL_FILE}" ]; then
				${objcopy} --only-keep-debug "${_dst}" "${_dst}".debug
				${strip} -g "${_dst}"
				${objcopy} --add-gnu-debuglink="${_dst}".debug "${_dst}"

				if [ -n "${_ident}" ]; then
					${chown} ${_ident} "${_dst}".debug || true
				fi
				${chmod} 0644 "${_dst}".debug

				echo "${rm} -f \"\${DESTDIR}${_xdst}\".debug" >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
			fi
		elif [ -n "${strip}" ]; then
			${strip} "${_dst}"
		fi
	fi

	if [ -n "${_ident}" ]; then
		${chown} ${_ident} "${_dst}" || true
	fi
	${chmod} ${_mode} "${_dst}"
}

__copyfile() {
	___docopychownfile "${1}" "${2}" '' "${3}" "${4}"
}

__copychownfile() {
	___docopychownfile "${1}" "${2}" "${3}" "${4}" "${5}"
}

cd "${CWDDIR}" || exit 11

echo '#!'"${SHELL}"' -' > "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo '#@ Uninstall script for '"${VAL_UAGENT}" >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo 'DESTDIR="'${DESTDIR}'"' >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo 'DESTDIR=' >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
echo >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"

[ -n "${DESTDIR}" ] && __mkdir ''
__mkdir "${VAL_BINDIR}"
__mkdir "${VAL_MANDIR}/man1"
__mkdir "${VAL_SYSCONFDIR}"

__copyfile y 0755 "${OBJDIR}"/"${VAL_UAGENT}" "${VAL_BINDIR}"/"${VAL_UAGENT}"
__copyfile n 0444 "${OBJDIR}"/uman.1 "${VAL_MANDIR}"/man1/"${VAL_UAGENT}".1
if [ -f "${DESTDIR}${VAL_SYSCONFDIR}/${VAL_SYSCONFRC}" ]; then :; else
	__copyfile n 0444 "${OBJDIR}"/urc.rc "${VAL_SYSCONFDIR}/${VAL_SYSCONFRC}"
fi

if [ "${OPT_DOTLOCK}" != 0 ]; then
	__mkdir "${VAL_LIBEXECDIR}"

	m='o=rx' o=
	#if [ -n "${_____PRIVSEP_GROUP}" ]; then
	#	 m="g=rxs,${m}" o=":${VAL_PRIVSEP_GROUP}"
	#else
		m="g=rx,${m}"
	#fi
	if [ -n "${VAL_PS_DOTLOCK_USER}" ]; then
		m="u=rxs,${m}" o="${VAL_PS_DOTLOCK_USER}${o}"
	else
		m="u=rx,${m}"
	fi;
	__copychownfile y "${m}" "${o}" \
		"${OBJDIR}"/"${VAL_PS_DOTLOCK}" "${VAL_LIBEXECDIR}/${VAL_PS_DOTLOCK}"
fi

if [ -z "${DESTDIR}" ]; then
	__copyfile n 0755 "${OBJDIR}/${VAL_UAGENT}-uninstall.sh" "${VAL_BINDIR}/${VAL_UAGENT}-uninstall.sh"
else
	echo "${rm} -f \"\${DESTDIR}${VAL_BINDIR}/${VAL_UAGENT}\"-uninstall.sh" >> "${OBJDIR}/${VAL_UAGENT}-uninstall.sh"
fi

# s-sht-mode
