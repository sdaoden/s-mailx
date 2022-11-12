#!/bin/sh -
#@ Adjust rc file according to configuration settings (sourced in environment).
#
# Public Domain

LC_ALL=C

VERSION=$(${SHELL} "${TOPDIR}"mk/make-version.sh query)
export VERSION

< "$1" > "$2" exec ${awk} '
	BEGIN {written = 0}
	/^#--MKRC-START--/,/^#--MKRC-END--/{
		if(written == 1)
			next
		written = 1
		OFS = ""
		ln = tolower(ENVIRON["VAL_UAGENT"])
		cn = toupper(substr(ln, 1, 1)) substr(ln, 2)
		print "#@ ", ENVIRON["VAL_SYSCONFDIR"], "/", ENVIRON["VAL_SYSCONFRC"]
		print "#@ Configuration file for ", cn, " v", ENVIRON["VERSION"], "."
		OFS = " "
		next
	}
	{print}
'

# s-sht-mode
