#!/bin/sh -
#@ Remove C++ code from include/su via special CXX_DOXYGEN markers.
#
# Public Domain

LC_ALL=C

: ${sed:=sed}
: ${git:=git}

echo 'include/su/*.h: stripping C++ code'
cd include/su || exit 1
${sed} -i'' -e '/ CXX_DOXYGEN/,/ @CXX_DOXYGEN/d' *.h
${git} add .

# s-sht-mode
