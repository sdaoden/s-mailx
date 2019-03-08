#!/bin/sh -
#@ make-mime-types.sh
#@ Read mime.types file from STDIN and generate S-nail builtin rules on STDOUT.
# Public Domain

: ${awk:=awk}

LC_ALL=C

exec ${awk} '
   function add(mt, ln){
      gsub(/[[:space:]]+/, " ", ln);
      i = split(ln, i_a);
      e = "";
      for(j = 1; j <= i; ++j){
         k = i_a[j];
         e = (e ? e " " : "") k
      }
      if(e){
         if(e_a[mt])
            e_a[mt] = e_a[mt] " ";
         e_a[mt] = e_a[mt] e
      }
   }
   /^[[:space:]]*#/{next}
   /^[[:space:]]*$/{ltype = ""; next}
   /^[[:space:]]/{
      if(!ltype)
         print "FAULTY CONTINUATION: " $0 >> "/dev/stderr";
      add(ltype, $0);
      next
   }
   /^(\?([thHq])? )?[[:alpha:]]/{
      if($1 ~ /^\?([thHq])?$/){
         pa = $1;
         $1 = $2;
         $2 = ""
      }else
         pa = "";
      if($1 !~ /^([[:alnum:]]+)\/([[:alnum:]_+-]+)$/)
         print "FAULTY MIME TYPE: <" $1 ">" >> "/dev/stderr";
      ltype = $1; $1 = "";
      if(pa)
         p_a[ltype] = pa;
      if(!nt_a[ltype])
         no_a[++no_ai] = nt_a[ltype] = ltype;
      add(ltype, $0)
   }
   END{
      for(z = 1; z <= no_ai; ++z){
         t = no_a[z];
         j = index(t, "/");
         mt = toupper(substr(t, 1, j - 1));
         j = substr(t, j + 1);
         l = length(j);
         if(!p_a[t])
            mt = "_MT_" mt;
         else{
            tm = p_a[t];
            if(tm ~ /^\?t?$/)
               mt = "_MT_" mt " | a_MT_TM_PLAIN";
            else if(tm ~ /^\?h$/)
               mt = "_MT_" mt " | a_MT_TM_SOUP_h";
            else if(tm ~ /^\?H$/)
               mt = "_MT_" mt " | a_MT_TM_SOUP_H";
            else if(tm ~ /^\?q$/)
               mt = "_MT_" mt " | a_MT_TM_QUIET";
         }
         print "   {" mt ", " l ", \"" j e_a[t] "\"},"
      }
   }
'

# s-sh-mode