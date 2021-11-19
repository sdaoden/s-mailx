#!/bin/sh -
#@ Set SU version number, interactively.
#
# Public Domain

LC_ALL=C

HF=include/su/code.h
HF=/tmp/code.h
: ${grep:=grep}
: ${sed:=sed}

v=$(${grep} -F 'define su_VERSION ' ${HF})
[ ${?} -eq 0 ] || exit 11

v=${v##* }
v=${v%*u}
ma=$(( (v >> 24) & 0x0FF ))
mi=$(( (v >> 12) & 0xFFF ))
up=$(( (v      ) & 0xFFF ))

printf 'Current version: %s (%s.%s.%s)\nNew version: ' \
   "${v}" "${ma}" "${mi}" "${up}"
read v
printf 'Is %s correct? ' "${v}"
read a
case "${a}" in
Y*|y*)
   ;;
*)
   echo >&2 'Then not'
   exit 21
   ;;
esac

# Primitive
if echo "${v}" |
     ${grep} -qE '^[[:digit:]]{1,3}\.[[:digit:]]{1,4}\.[[:digit:]]{1,4}$'; then
   xma=${v%%.*}
   v=${v#*.}
   xmi=${v%%.*}
   v=${v#*.}
   xup=${v}
elif echo "${v}" | ${grep} -qE '^[[:digit:]]{1,3}\.[[:digit:]]{1,4}$'; then
   xma=${v%%.*}
   v=${v#*.}
   xmi=${v%%.*}
   xup=0
elif echo "${v}" | ${grep} -qE '^[[:digit:]]{1,3}$'; then
   xma=${v%%.*}
   xmi=0
   xup=0
else
   echo >&2 'Version must be MAJOR[.MINOR[.UPDATE]]'
   exit 31
fi

if [ $xma -gt 255 ] || [ $xmi -gt 4095 ] || [ $xup -gt 4095 ]; then
   echo >&2 'Version maximums are 0xFF.0xFFF.0xFFF'
   exit 32
fi

nver=$(( ($xma << 24) | ($xmi << 12) | $xup ))
nver=$(printf '0x%Xu\n' ${nver})
echo 'SU version is '${nver}

printf 'Write that to %s? ' "${HF}"
read a
case "${a}" in
Y*|y*)
   ${sed} -i'' -E \
         -e 's/^#define su_VERSION .+$/#define su_VERSION '${nver}'/' \
         -e 's/^#define su_VERSION_STRING .+$/'\
'#define su_VERSION_STRING "'${xma}.${xmi}.${xup}'"/' "${HF}"
   ;;
*)
   echo >&2 'Then not'
   exit 41
   ;;
esac

# s-sh-mode
