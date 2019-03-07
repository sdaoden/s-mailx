#!/bin/sh -
#@ Find an executable command within a POSIX shell.
#@ which(1) is not standardized, and command(1) -v may return non-executable,
#@ so here is how it is possible to really find a usable executable file.
#@ Use like this:
#@    thecmd_testandset chown chown ||
#@       PATH="/sbin:${PATH}" thecmd_set chown chown ||
#@       PATH="/usr/sbin:${PATH}" thecmd_set_fail chown chown
#@ or
#@    thecmd_testandset_fail MAKE make
#@ or
#@    MAKE=/usr/bin/make thecmd_testandset_fail MAKE make
#
# 2017 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
# Thanks to Robert Elz (kre).
# Public Domain

## First of all, the actual functions need some environment:

if [ -z "${SU_FIND_COMMAND_INCLUSION}" ]; then
   VERBOSE=1

   ( set -o noglob ) >/dev/null 2>&1 && noglob_shell=1 || unset noglob_shell

   msg() {
      fmt=${1}
      shift
      printf >&2 -- "${fmt}\n" "${@}"
   }
fi

## The actual functions

acmd_test() { fc__acmd "${1}" 1 0 0; }
acmd_test_fail() { fc__acmd "${1}" 1 1 0; }
acmd_set() { fc__acmd "${2}" 0 0 0 "${1}"; }
acmd_set_fail() { fc__acmd "${2}" 0 1 0 "${1}"; }
acmd_testandset() { fc__acmd "${2}" 1 0 0 "${1}"; }
acmd_testandset_fail() { fc__acmd "${2}" 1 1 0 "${1}"; }
thecmd_set() { fc__acmd "${2}" 0 0 1 "${1}"; }
thecmd_set_fail() { fc__acmd "${2}" 0 1 1 "${1}"; }
thecmd_testandset() { fc__acmd "${2}" 1 0 1 "${1}"; }
thecmd_testandset_fail() { fc__acmd "${2}" 1 1 1 "${1}"; }

##  --  >8  - -  8<  --  ##

fc__pathsrch() { # pname=$1 exec=$2 varname=$3 verbok=$4
   fcps__pname=$1 fcps__exec=$2 fcps__varname=$3 fcps__verbok=$4
   # Manual search over $PATH
   fcps__oifs=${IFS} IFS=:
   [ -n "${noglob_shell}" ] && set -o noglob
   set -- ${PATH}
   [ -n "${noglob_shell}" ] && set +o noglob
   IFS=${fcps__oifs}
   for fcps__path
   do
      if [ -z "${fcps__path}" ] || [ "${fcps__path}" = . ]; then
         if [ -d "${PWD}" ]; then
            fcps__path=${PWD}
         else
            fcps__path=.
         fi
      fi
      if [ -f "${fcps__path}/${fcps__exec}" ] &&
            [ -x "${fcps__path}/${fcps__exec}" ]; then
         [ -n "${VERBOSE}" ] && [ ${fcps__verbok} -ne 0 ] &&
            msg ' . ${%s} ... %s' \
               "${fcps__pname}" "${fcps__path}/${fcps__exec}"
         [ -n "${fcps__varname}" ] &&
            eval "${fcps__varname}"="${fcps__path}/${fcps__exec}"
         return 0
      fi
   done
   return 1
}

fc__acmd() {
   fca__pname=${1} fca__dotest=${2} fca__dofail=${3} \
      fca__verbok=${4} fca__varname=${5}

   if [ "${fca__dotest}" -ne 0 ]; then
      eval fca__dotest=\$${fca__varname}
      if [ -n "${fca__dotest}" ]; then
         if fc__pathsrch "${fca__pname}" "${fca__dotest}" "${fca__varname}" \
               ${fca__verbok}; then
            return 0
         fi
         msg 'WARN: ignoring non-executable ${%s}=%s' \
            "${fca__pname}" "${fca__dotest}"
      fi
   fi

   # It may be an absolute path, check that first
   if [ "${fca__pname}" != "${fca__pname#/}" ] &&
         [ -f "${fca__pname}" ] && [ -x "${fca__pname}" ]; then
      [ -n "${VERBOSE}" ] && [ ${fca__verbok} -ne 0 ] &&
            msg ' . ${%s} ... %s' "${fca__pname}" "${fca__pname}"
      [ -n "${fca__varname}" ] && eval "${fca__varname}"="${fca__pname}"
      return 0
   fi

   if fc__pathsrch "${fca__pname}" "${fca__pname}" "${fca__varname}" \
         ${fca__verbok}; then
      return 0
   fi

   [ -n "${fca__varname}" ] && eval "${fca__varname}"=
   [ ${fca__dofail} -eq 0 ] && return 1
   msg 'ERROR: no trace of utility '"${fca__pname}"
   exit 1
}

# s-sh-mode
