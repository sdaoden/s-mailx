#!/bin/sh -
#@ Round trip quote strings in POSIX shell.  E.g.,
#@    set -- x 'a \ b' "foo'" "\\'b\\a\\r\\" Aä
#@    printf "%s: <%s><%s><%s><%s><%s>\n" "$#" "${1}" "${2}" "${3}" "$4" "$5"
#@    saved_parameters=`quote_rndtrip "$@"`
#@    eval "set -- $saved_parameters"
#@    printf "%s: <%s><%s><%s><%s><%s>\n" "$#" "${1}" "${2}" "${3}" "$4" "$5"
#
# 2017 Robert Elz (kre).
# 2017 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
# Public Domain

# Though slower use a subshell version instead of properly restoring $IFS and
# flags, as elder shells may not be able to properly restore flags via "set +o"
# as later standardized in POSIX, and it seems overkill to handle all possible
# forms of output that "set +o" may or may not actually generate
quote__rndtrip() (
   case "$1" in
   *\'*) ;;
   *) printf "'%s'" "$1"; return 0;;
   esac
   __A__="$1" __S__= __E__=
   while case "$__A__" in
      \'*)  __A__=${__A__#?}; __S__="${__S__}\\\\'";;
      *\')  __A__=${__A__%?}; __E__="${__E__}\\\\'";;
      '')   printf "${__S__}${__E__}"; exit 0;;
      *) false;;
      esac
   do
      continue
   done
   IFS=\'
   set -f
   set -- $__A__
   _result_="${1}"
        shift
   for __A__
   do
      _result_="${_result_}'\\''${__A__}"
   done
   printf "${__S__}'%s'${__E__}" "${_result_}"
   exit 0
)

quote_rndtrip() {
   j=
   for i
   do
      [ -n "$j" ] && printf ' '
      j=' '
      quote__rndtrip "$i"
   done
}

quote_string() {
   j=
   for i
   do
      [ -n "$j" ] && printf '\\ '
      j=' '
      quote__rndtrip "$i"
   done
}

# s-sh-mode
