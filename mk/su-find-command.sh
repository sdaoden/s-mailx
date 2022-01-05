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
#@ _However_, in order to support prefilled variables like "awk='busybox awk'"
#@ spaces in command names found via path search are not supported.
#@ That is to say that we take user-prefilled variable names with spaces as
#@ granted, and actively fail to find commands with spaces ourselfs; like this
#@ users of these functions can simply say: $VAR args, not "$VAR" args.
#
# 2017 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

   # Commands with spaces are not found
   if [ "${fcps__exec}" != "${fcps__exec#* }" ]; then
      [ -n "${VERBOSE}" ] && [ ${fcps__verbok} -ne 0 ] &&
         msg ' . ${%s} ... %s (has spaces, CANNOT be found)' \
            "${fcps__pname}" "${fcps__exec}"
      return 1
   fi

   # It may be an absolute path, check that first
   if [ "${fcps__exec}" != "${fcps__exec#/}" ] &&
         [ -f "${fcps__exec}" ] && [ -x "${fcps__exec}" ]; then
      [ -n "${VERBOSE}" ] && [ ${fcps__verbok} -ne 0 ] &&
         msg ' . ${%s} ... %s' \
            "${fcps__pname}" "${fcps__exec}"
      [ -n "${fcps__varname}" ] &&
         eval "${fcps__varname}"="${fcps__exec}"
      return 0
   fi

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

   # Is the variable prefilled?
   if [ "${fca__dotest}" -ne 0 ]; then
      eval fca__dotest=\$${fca__varname}
      if [ -n "${fca__dotest}" ]; then
         # It could be something like "busybox awk".  So if there is any
         # whitespace in a given variable, this cannot truly be solved in an
         # automatic fashion, we need to take it for granted
         if [ "${fca__dotest}" = "${fca__dotest#* }" ] ||
               [ -f "${fca__dotest}" ]; then :; else
            [ -n "${VERBOSE}" ] && [ ${fca__verbok} -ne 0 ] &&
               msg ' . ${%s} ... %s (spacy user data, unverifiable)' \
                  "${fca__pname}" "${fca__dotest}"
            return 0
         fi

         if fc__pathsrch "${fca__pname}" "${fca__dotest}" "${fca__varname}" \
               ${fca__verbok}; then
            return 0
         fi
         msg 'WARN: ignoring non-executable ${%s}=%s' \
            "${fca__pname}" "${fca__dotest}"
      fi
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
