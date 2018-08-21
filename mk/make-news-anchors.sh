#!/bin/sh -
#@ make-news-anchors.sh
#@ Expand *XY*# / $XY# / -XY# / `XY'# / `~XY'# / "XY"# style anchors
#@ so that the anchor matches the number given in ANCHORFILE.
#@ The number sign may be followed by space, question-mark ? or a number.
#@ We always expand STDIN to STDOUT, but only in the range
#@    ChangeLog  ..  ^(Appendix|git\(1\) shortlog)  (or EOF, of course)
#@ The ANCHORFILE can be produced by
#@    $ < manual.mdoc mdocmx.sh |
#@          MDOCMX_ENABLE=1 groff -U -mdoc -dmx-anchor-dump=/tmp/anchors \
#@             -dmx-toc-force=tree >/dev/null
# Public Domain

: ${awk:=awk}

syno() {
   if [ ${#} -gt 0 ]; then
      echo >&2 "ERROR: ${*}"
      echo >&2
   fi
   echo >&2 'Synopsis: make-news-anchors.sh ANCHORFILE'
   exit 1
}

[ ${#} -eq 1 ] || syno
[ -f "${1}" ] || syno 'the given anchorfile does not exist'

APO=\'
${awk} -v anchorfile="${1}" '
BEGIN{hot = 0}
/^(NOTES|ChangeLog)/{
   hot = 1
   print
   next
}
/^(Appendix|git\(1\) shortlog)/{
   hot = -1
   print
   next
}
{
   if(hot <= 0){
      print
      next
   }
   any = 0
   res = ""
   s = $0
   while(match(s,
         /(^|\(|[[:space:]]+)("[^"]+"|\*[^\*]+\*|`[^'${APO}']+'${APO}'|[-~][-#\/:_.[:alnum:]]+|\$[_[:alnum:]]+)#(\?|[0-9]+)?/))
   {
      any = 1
      pre = (RSTART > 1) ? substr(s, 1, RSTART - 1) : ""
      mat = substr(s, RSTART, RLENGTH)
      s = substr(s, RSTART + RLENGTH)

      # Unfortunately groups are not supported
      if(match(mat, /^(\(|[[:space:]]+)/) != 0 && RLENGTH > 0){
         pre = pre substr(mat, 1, RLENGTH)
         mat = substr(mat, RSTART + RLENGTH)
      }

      match(mat, /#(\?|[0-9]+)?$/)
      mat = substr(mat, 1, RSTART - 1)
      res = res pre mat "#"

      if(mat ~ /^`/){ # Cm, Ic
         mat = substr(mat, 2, length(mat) - 2)
         t = 1
      }else if(mat ~ /^\*/){ # Va
         mat = substr(mat, 2, length(mat) - 2)
         t = 2
      }else if(mat ~ /^\$/){ # Ev, Dv
         mat = substr(mat, 2, length(mat) - 1)
         t = 3
      }else if(mat ~ /^-/){ # Fl
         mat = substr(mat, 2, length(mat) - 1)
         t = 4
      }else if(mat ~ /^\"/){ # Sh, Ss.  But: "catch-all"
         mat = substr(mat, 2, length(mat) - 2)
         t = 5
      }else
         t = 0

      # Insufficient, of course
      gsub("\\\\", "\\e", mat)

      ano = got = 0
      while(getline < anchorfile){
         if(t == 1){
            if($2 != "Cm" && $2 != "Ic")
               continue
         }else if(t == 2){
            if($2 != "Va")
               continue
         }else if(t == 3){
            if($2 != "Ev" && $2 != "Dv")
               continue
         }else if(t == 4){
            if($2 != "Fl")
               continue
         }else if(t == 0){
            if($2 == "Cm" || $2 == "Ic" ||
                  $2 == "Va" ||
                  $2 == "Ev" || $2 == "Dv" ||
                  $2 == "Fl")
               continue
         }

         if(!got)
            ano = $1
         $1 = $2 = ""
         match($0, /^[[:space:]]*/)
         $0 = substr($0, RLENGTH + 1)

         if($0 == mat){
            if(got)
               print "WARN: ambiguous: \"" mat "\"" > "/dev/stderr"
            got = 1
         }
      }
      close(anchorfile)
      if(!got){
         print "ERROR: no anchor for \"" mat "\"" > "/dev/stderr"
         res = res "?"
      }else
         res = res ano
   }
   if(any && length(s))
      res = res s
   print any ? res : s
}
'

# s-sh-mode
