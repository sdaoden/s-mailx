#!/bin/sh -
#@ Generate builtin RC file (configuration sourced in environment).
#
# Public Domain

LC_ALL=C
: ${awk:=awk}

< "$1" > "$2" exec ${awk} '
   BEGIN{
      lines = 0
      dat = ""

      # Sun xpg4/bin/awk expands those twice:
      #  Notice that backslash escapes are interpreted twice, once in
      #  lexical processing of the string and once in processing the
      #  regular expression.
      dblexp = "\""
      gsub(/"/, "\\\\\"", dblexp)
      dblexp = (dblexp == "\134\"")
   }

   function quote(s){
      gsub("\"", (dblexp ? "\\\\\"" : "\134\""), s)
      return s
   }

   /^[	 ]*#/{next}
   /^[	 ]*$/{next}
   /\\$/{
      sub("^[[:space:]]*", "")
      sub("\\\\$", "")
      dat = dat $0
      next
   }
   {
      sub("^[[:space:]]*", "")
      print "/*{*/\"" quote(dat $0) "\"/*}*/,"
      ++lines;
      dat = ""
      next
   }

   END{print "#define a_GO_BLTIN_RC_LINES_CNT " lines}
'

# s-sht-mode
