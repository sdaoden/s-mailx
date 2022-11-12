#!/bin/sh -
#@ A "pseudo cross-build $CC" to "test" at least CROSS_BUILD "config".
#
# Public Domain

# Do we have to go via a real C compiler in preprocessor mode?
dowemayhave() {
	while [ $# -gt 0 ]; do
		[ "$1" = -E ] && return 0
		shift
	done
	return 1
}

if dowemayhave "$@"; then
	SU_FIND_COMMAND_INCLUSION=1 . "${TOPDIR}"mk/su-find-command.sh

	if acmd_set CC clang || acmd_set CC gcc ||
			acmd_set CC tcc || acmd_set CC pcc ||
			acmd_set CC c89 || acmd_set CC c99; then
		exec $CC "$@"
	else
		echo >&2 'boing booom tschak'
		echo >&2 'ERROR: I cannot find a compiler!'
		echo >&2 'No clang(1), gcc(1), tcc(1), pcc(1), c89(1) or c99(1).'
	fi
else
	[ $# -eq 0 ] && exit 1
	while [ $# -gt 0 ]; do
		if [ "$1" = -o ]; then
			printf '\n' > "$2"
			exit 0
		fi
		shift
	done
	exit 0
fi

exit 1
# s-sht-mode
