#!/bin/sh -
#@ Out-of-tree compilation support, à la
#@    $ cd /tmp && mkdir build && cd build &&
#@       ~/src/nail.git/make-emerge.sh && make tangerine DESTDIR=.ddir
# Public Domain

# See make-config.sh, the source of all this!

config_exit() {
   exit ${1}
}

msg() {
   fmt=${1}
   shift
   printf >&2 -- "${fmt}\\n" "${@}"
}

unset -f command
check_tool() {
   n=${1} i=${2} opt=${3:-0}
   # Evaluate, just in case user comes in with shell snippets (..well..)
   eval i="${i}"
   if type "${i}" >/dev/null 2>&1; then # XXX why have i type not command -v?
      [ -n "${VERBOSE}" ] && msg ' . ${%s} ... %s' "${n}" "${i}"
      eval ${n}=${i}
      return 0
   fi
   if [ ${opt} -eq 0 ]; then
      msg 'ERROR: no trace of utility %s' "${n}"
      config_exit 1
   fi
   return 1
}

syno() {
   if [ ${#} -gt 0 ]; then
      echo >&2 "ERROR: ${*}"
      echo >&2
   fi
   echo >&2 'Synopsis: SOURCEDIR/make-emerge.sh [from within target directory]'
   exit 1
}

[ ${#} -eq 0 ] || syno

# Rude simple, we should test for Solaris, but who runs this script?
if [ -d /usr/xpg4 ]; then
   PATH=/usr/xpg4/bin:${PATH}
fi

check_tool awk "${awk:-`command -v awk`}"
check_tool dirname "${dirname:-`command -v dirname`}"
check_tool pwd "${pwd:-`command -v pwd`}"

srcdir=`${dirname} ${0}`
blddir=`${pwd}`
if [ "${srcdir}" = . ]; then
   echo >&2 'This is not out of tree?!'
   exit 1
fi
echo 'Initializing out-of-tree build.'
echo 'Source directory: '"${srcdir}"
echo 'Build directory : '"${blddir}"
srcdir="${srcdir}"/

${awk} -v srcdir="${srcdir}" -v blddir="${blddir}" '
   {
      gsub(/^SRCDIR=\.\/$/, "SRCDIR=" srcdir)
      print
   }
   ' < "${srcdir}"makefile > ./makefile

cp "${srcdir}"config.h ./
cp "${srcdir}"gen-version.h ./
cp "${srcdir}"make.rc ./

echo 'You should now be able to proceed as normal (e.g., "$ make all")'

# s-sh-mode
