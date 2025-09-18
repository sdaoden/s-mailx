#!/bin/sh -
#@ Adjust manual according to configuration settings (sourced in environment).
#
# Public Domain

CNFIN="$TOPDIR"mx-config.7.in

LC_ALL=C

VERSION=$($SHELL "$TOPDIR"mk/make-version.sh query)
export VERSION

< "$2" > "$3" exec $awk -v X="$1" -v CNFIN="$CNFIN" '
	BEGIN {written = 0}
	/\.\\"--MKMAN-START--/,/\.\\"--MKMAN-END--/{
		if(written++ != 0)
			next
		OFS = ""
		print ".ds VV \\\\%v", ENVIRON["VERSION"]

		un = toupper(ENVIRON["VAL_UAGENT"])
			ln = tolower(un)
			cn = toupper(substr(ln, 1, 1)) substr(ln, 2)
			print ".ds XX \\\\%", un
			print ".ds Xx \\\\%", cn
			print ".ds xX \\\\%", ln
		path = ENVIRON["VAL_SYSCONFRC"]
			gsub("/", "/\\:", path)
			print ".ds UR \\\\%", path
		path = ENVIRON["VAL_MAILRC"]
			gsub("/", "/\\:", path)
			print ".ds ur \\\\%", path

		path = ENVIRON["VAL_DEAD"]
			gsub("/", "/\\:", path)
			print ".ds VD \\\\%", path
		path = ENVIRON["VAL_MBOX"]
			gsub("/", "/\\:", path)
			print ".ds VM \\\\%", path
		path = ENVIRON["VAL_NETRC"]
			gsub("/", "/\\:", path)
			print ".ds VN \\\\%", path
		path = ENVIRON["VAL_TMPDIR"]
			gsub("/", "/\\:", path)
			print ".ds VT \\\\%", path

		path = ENVIRON["VAL_MIME_TYPES_SYS"]
			gsub("/", "/\\:", path)
			print ".ds vS \\\\%", path
		path = ENVIRON["VAL_MIME_TYPES_USR"]
			gsub("/", "/\\:", path)
			print ".ds vU \\\\%", path

		OFS = " "
		next
	}
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
