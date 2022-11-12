#!/bin/sh -
#@ Round trip quote strings in POSIX shell.  For example
#@   set -- x 'a \ b' "foo'" "\\'b\\a\\r\\" Aä
#@   printf "%s: <%s><%s><%s><%s><%s>\n" "$#" "${1}" "${2}" "${3}" "$4" "$5"
#@   saved_parameters=`quote_rndtrip "$@"`
#@   eval "set -- $saved_parameters"
#@   printf "%s: <%s><%s><%s><%s><%s>\n" "$#" "${1}" "${2}" "${3}" "$4" "$5"
#
# 2017 Robert Elz (kre).
# 2017 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
# Public Domain

# Though slower use a subshell version instead of properly restoring $IFS
# and flags, as elder shells may not be able to properly restore flags via
# "set +o" as later standardized in POSIX, and it seems overkill to handle
# all possible forms of output "set +o" may or may not actually generate.
quote__rndtrip() (
	case "$1" in
	*\'*) ;;
	*) printf "'%s'" "$1"; return 0;;
	esac
	a="$1" s= e=
	while case "$a" in
		\'*) a=${a#?}; s="${s}\\\\'";;
		*\') a=${a%?}; e="${e}\\\\'";;
		'') printf "${s}${e}"; exit 0;;
		*) false;;
		esac
	do
		continue
	done
	IFS=\'
	set -f
	set -- $a
	r="${1}"
	shift
	for a
	do
		r="${r}'\\''${a}"
	done
	printf "${s}'%s'${e}" "${r}"
	exit 0
)

quote_rndtrip() (
	j=
	for i
	do
		[ -n "$j" ] && printf ' '
		j=' '
		quote__rndtrip "$i"
	done
)

quote_string() (
	j=
	for i
	do
		[ -n "$j" ] && printf '\\ '
		j=' '
		quote__rndtrip "$i"
	done
)

# s-sht-mode
