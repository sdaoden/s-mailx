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
         print "FAULTY CONTINUATION: " $0 >> "/dev/stderr"
      add(ltype, $0)
      next
   }
   /^(\?([thHq])? )?[a-zA-Z]/{
      if($1 ~ /^\?([thHq])?$/){
         pa = $1
         $1 = $2
         $2 = ""
      }else
         pa = ""
      if($1 !~ /^([0-9a-zA-Z]+)\/([0-9a-zA-Z_+-]+)$/)
         print "FAULTY MIME TYPE: <" $1 ">" >> "/dev/stderr"
      ltype = $1; $1 = ""
      if(pa)
         p_a[ltype] = pa
      if(!nt_a[ltype])
         no_a[++no_ai] = nt_a[ltype] = ltype
      add(ltype, $0)
   }
   END{
      for(z = 1; z <= no_ai; ++z){
         t = no_a[z]
         j = index(t, "/")
         mt = toupper(substr(t, 1, j - 1))
         j = substr(t, j + 1)
         l = length(j)
         if(!p_a[t])
            mt = "a_MT_" mt
         else{
            tm = p_a[t]
            if(tm ~ /^\?t?$/)
               mt = "a_MT_" mt " | a_MT_TM_PLAIN"
            else if(tm ~ /^\?h$/)
               mt = "a_MT_" mt " | a_MT_TM_SOUP_h"
            else if(tm ~ /^\?H$/)
               mt = "a_MT_" mt " | a_MT_TM_SOUP_H"
            else if(tm ~ /^\?q$/)
               mt = "a_MT_" mt " | a_MT_TM_QUIET"
         }
         print "   {" mt ", " l ", \"" j e_a[t] "\"},"
      }
   }
'

# s-sh-mode
