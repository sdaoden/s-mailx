#!/bin/sh -
#@ Update character-sets from IANA.
#@ Adapted from a smart awk script written by Gaetan Bisson, and adjusted.

url_cs='https://www.iana.org/assignments/character-sets/character-sets.xml'

: ${FMT:=txt} # txt,c,list
: ${FETCH:=} # non-empty: do it
: ${DBG:=} # non-empty: add more comments (FMT=c)
: ${MIME:=} # non-empty: only include preferred MIME name and normalizations (FMT=c)

: ${awk:=awk}
: ${curl:=curl}

datetime=$(date +'%FT%T%z')

download() (
	${curl} -v -o character-sets.xml ${url_cs}
	echo download ok: $?
	echo remember to adjust ASCII alias
)

process() {
	${awk} -F "[<>]" -v FMT="${FMT}" -v MIME="${MIME}" -v DBG="${DBG}" -v URL="${url_cs}" -v DT="${datetime}" '
		function err(){
			print "Bogus: " FILENAME " at line " FNR ": " $0 > "/dev/stderr"
			exit 1
		}
		# Normalize algorithm
		function c_norm(n){
			n = tolower(n)
			gsub("[[:punct:]]", " ", n)
			gsub("[[:space:]]+", " ", n)
			for(;;){
				o = match(n, "[[:lower:]][[:digit:]]")
				if(o == 0){
					o = match(n, "[[:digit:]][[:lower:]]")
					if(o == 0)
						break
				}
				n2 = substr(n, o + 1)
				n = substr(n, 1, o) " " n2
			}
			return n
		}

		BEGIN {all=header_dumped = parse = hot = datno = 0}
		/<registry/ {parse=1}
		/<\/registry/ {exit}
		/<record/{
			if(!parse) next
			if(hot) err()
			hot = 1
			n = mib = p = ""
			acnt = 0
		}
		/<name/ {if(!parse) next; if(!hot) err(); if(n) err(); n = $3}
		/<value/ {if(!parse) next; if(!hot) err(); if(mib) err(); mib = $3}
		/<alias/ {if(!parse) next; if(!hot) err(); aa[++acnt] = $3}
		/<preferred_alias/ {if(!parse) next; if(!hot) err(); if(p) err(); p = $3}
		/<\/record/{
			if(!parse) next; if(!hot) err()
			hot = 0

			if(!n) err()
			if(!p)
				p = n
			p = tolower(p)

			if(FMT == "txt"){
				if(!header_dumped++){
					print "# character-sets.txt, created " DT
					print "# Source: " URL
					print ""
				}

				print "Name: " n
				if(mib)
					print "MIBenum: " mib
				print "MIME: " p
				if(acnt > 0){
					for(i = 1; i <= acnt; ++i)
						print "Alias: " aa[i]
				}
				print ""
			}else if(FMT == "c"){
				if(!header_dumped++){
					print "/* IANA character-sets data, created " DT " */"
					print "/* Source: " URL " */"
					if(DBG)
						print ""
					print "static char const"
				}

				if(!mib)
					mib = "U16_MAX"

				++datno
				cdat[datno "mib"] = mib
				cdat[datno "name"] = p
				cdat[datno "mime_off"] = 0 # for now

				if(datno > 1)
					printf ","
				printf " * const a_iconv_cs_" datno "[] = {"
				if(DBG)
					printf "\n\t"
				printf "\"" p "\""
				if(MIME){
					cnt = 0
					cdat[datno "mime_off"] = "U8_MAX";
				}else{
					printf ",\"" n "\""
					cnt = 1
				}
				if(!MIME && acnt > 0){
					for(i = 1; i <= acnt; ++i){
						if(tolower(aa[i]) == p)
							cdat[datno "mime_off"] = cnt
						printf ",\"" aa[i] "\""
						++cnt
					}
				}
				cdat[datno "cnt"] = cnt

				if(DBG)
					printf "\n\t"
				cnt = 1
				cnorm[1] = c_norm(n)
				if(acnt > 0)
					for(i = 1; i <= acnt; ++i){
						n = c_norm(aa[i])
						for(j = 1;; ++j){
							if(cnorm[j] == n)
								break
							if(j == cnt){
								cnorm[++cnt] = n
								break;
							}
						}
					}
				cdat[datno "norm_cnt"] = cnt
				for(i = 1; i <= cnt; ++i)
					printf ",\"" cnorm[i] "\""

				if(DBG){
					printf "\n\t/* mib=" cdat[datno "mib"] " name=" cdat[datno "name"]
					printf " cnt=" cdat[datno "cnt"] " mime_off=" cdat[datno "mime_off"]
					printf " norm_cnt=" cdat[datno "norm_cnt"] " */\n"
				}

				printf "}\n"
			}else if(FMT == "list"){
				print n
				if(acnt > 0){
					for(i = 1; i <= acnt; ++i)
						print aa[i]
				}
			}else{
				print "unknown FMT: " FMT > "/dev/stderr"
				exit 64
			}
		}
		END{
			if(FMT == "c"){
				printf ";\n"
				if(DBG){
					print ""
				}
				print "static struct a_iconv_cs const a_iconv_db[] = {"
				for(i = 1; i <= datno; ++i){
					if(DBG)
						printf "\t"
					printf "{" cdat[i "mib"] "," cdat[i "cnt"] "," cdat[i "mime_off"]
					printf "," cdat[i "norm_cnt"] ",{0,},a_iconv_cs_" i "},"
					if(DBG)
						printf " /* " cdat[i "name"] " */"
					printf "\n"
				}
				print "};"
			}
		}
	' < character-sets.xml
	[ ${?} -eq 0 ] || exit 30
}

if [ -n "${FETCH}" ]; then
	download || exit ${?}
fi
process

# s-itt-mode
