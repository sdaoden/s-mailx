#!/bin/sh -
#@ Generate builtin mime.types (configuration sourced in environment).
#
# Public Domain

LC_ALL=C

< "$1" > "$2" exec ${awk} '
   function add(mt, ln){
      gsub(/[ 	]]+/, " ", ln)
      i = split(ln, i_a)
      e = ""
      for(j = 1; j <= i; ++j){
         k = i_a[j]
         e = (e ? e " " : "") k
      }
      if(e){
         if(e_a[mt])
            e_a[mt] = e_a[mt] " "
         e_a[mt] = e_a[mt] e
      }
   }

   /^[ 	]*#/{next}
   /^[ 	]*$/{ltype = ""; next}
   /^[ 	]/{
      if(!ltype)
         print "! FAULTY MIME TYPE CONTINUATION: " $0 >> "/dev/stderr"
      add(ltype, $0)
      next
   }
   /^(\?([*thHq]*) )?[a-zA-Z]/{
      # type-marker?
      if($1 ~ /^\?([*thHq]*)$/){
         pa = $1
         $1 = $2
         $2 = ""
      }else
         pa = ""

      if($1 !~ /^([0-9a-zA-Z]+)\/([0-9a-zA-Z_+-]+)$/)
         print "! FAULTY MIME TYPE: <" $1 ">" >> "/dev/stderr"
      ltype = $1; $1 = ""

      if(!nt_a[ltype]){
         if(pa)
            p_a[ltype] = pa
         no_a[++no_ai] = nt_a[ltype] = ltype
      }else if(pa && p_a[ltype])
         print "! TYPE-MARKER IGNORED WHEN EXTENDING MIME TYPE: <" \
            ltype ">: " $0 >> "/dev/stderr"

      add(ltype, $0)
   }

   END{
      for(z = 1; z <= no_ai; ++z){
         t = no_a[z]
         j = index(t, "/")
         mt = toupper(substr(t, 1, j - 1))
         j = substr(t, j + 1)
         l = length(j)

         # type-marker?
         if(!p_a[t])
            mt = "a_MT_" mt
         else{
            tm = p_a[t]
            tmx = ""
            aster = 0

            while(length(tm) > 0){
               x = substr(tm, 1, 1)
               tm = substr(tm, 2)
               if(x == "*"){
                  if(aster++ != 0){
                     print "! DUPLICATE * MIME TYPE-MARKER: <" no_a[z] ">" \
                        " <" p_a[t] ">" >> "/dev/stderr"
                  }
               }else{
                  if(tmx)
                     print "! INVALID MIME TYPE-MARKER: <" no_a[z] ">" \
                        " <" p_a[t] ">" >> "/dev/stderr"
                  if(x == "t")
                     tmx = "a_MT_TM_PLAIN"
                  else if(x == "h")
                     tmx = "a_MT_TM_SOUP_h"
                  else if(x == "H")
                     tmx = "a_MT_TM_SOUP_H"
                  else if(x == "q")
                     tmx = "a_MT_TM_QUIET"
               }
            }

            mt = "a_MT_" mt
            if(!tmx && !aster)
               tmx = "a_MT_TM_PLAIN"
            if(aster)
               mt = mt " | a_MT_TM_HDL_ONLY"
            if(tmx)
               mt = mt " | " tmx
         }

         print "   {" mt ", " l ", \"" j e_a[t] "\"},"
      }
   }
'

# s-sh-mode
