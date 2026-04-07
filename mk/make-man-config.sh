#!/bin/sh -
#@ Expand mx-config.7 content from mx-config.7.in.
#
# Public Domain

CNFIN="$TOPDIR"mx-config.7.in

LC_ALL=C

VERSION=$($SHELL "$TOPDIR"mk/make-version.sh query)
export VERSION

< "$1" > "$2" exec $awk -v CNFIN="$CNFIN" '
	BEGIN {written = 0}
	/\.\\"--MKROFF-START--/,/\.\\"--MKROFF-END--/{
		inli = el = 0
		while((getline < CNFIN) > 0){
			if($0 ~ /^#\./){
				if(inli){
					inli = 0
					print ".Ed"
				}
				print substr($0, 3)
				continue
			}
			if($0 ~ /^#[@#]/)
				continue
			if(length($0) == 0){
				if(el++ != 0)
					continue
			}else
				el = 0
			if(!inli){
				inli = 1
				print ".Bd -literal -offset indent"
			}
			gsub(/\\/, "\\e")
			if($0 ~ /^#!/){
				i = "#" substr($0, 3)
				print i
			}else
				print
		}
		close(CONFIN)
		if(inli)
			print ".Ed"
		next
	}
	{print}
'

# s-sht-mode
