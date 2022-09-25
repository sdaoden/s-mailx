#!/bin/sh -
#@ mdocmx.sh - mdocmx(7) preprocessor for single-pass troff.
#@ mdocmx(7) extends the mdoc(7) semantic markup language by references,
#@ allowing mdoc(7) to create anchors and table of contents.
#@ Synopsis: mdocmx[.sh] [:-v:] [-t | -T Sh|sh|Ss|ss  [-c]] [FILE]
#@ -v: increase verbosity
#@ -t: whether -toc lines shall be expanded to a flat .Sh TOC
#@ -T: whether -toc lines shall be expanded as specified: only .Sh / .Sh + .Ss
#@ -c: only with -t or -T: whether compact TOC display shall be generated
#@ Set $AWK environment to force a special awk(1) interpreter.
#
# Written 2014 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
# Public Domain

: ${TMPDIR:=/tmp}
: ${ENV_TMP:="${TMPDIR}:${TMP}:${TEMP}"}

#  --  >8  --  8<  --  #

# For heaven's sake add special treatment for SunOS/Solaris
if [ -d /usr/xpg4/bin ]; then
  PATH=/usr/xpg4/bin:$PATH
  export PATH
fi

#AWK=
EX_OK=0
EX_USAGE=64
EX_DATAERR=65
EX_TEMPFAIL=75

V=0 T= TT= F=

( set -o noglob ) >/dev/null 2>&1 && set -o noglob

find_awk() {
  [ -n "${AWK}" ] && return 0
  i=${IFS}
  IFS=:
  set -- ${PATH}:/bin
  IFS=${i}
  # for i; do -- new in POSIX Issue 7 + TC1
  for i
  do
    if [ -z "${i}" ] || [ "${i}" = . ]; then
      if [ -d "${PWD}" ]; then
        i=${PWD}
      else
        i=.
      fi
    fi
    for j in n m '' g; do
      AWK="${i}/${j}awk"
      [ -f "${AWK}" ] && [ -x "${AWK}" ] && return 0
    done
  done
  return 1
}

synopsis() {
  ex=${1} msg=${2}
  [ -n "${msg}" ] && echo >&2 ${msg}
  [ ${ex} -eq 0 ] && f=1 || f=2
  ( echo "${0##*/}" ) >/dev/null 2>&1 && eval 'p="${0##*/}"' || p="${0}"
  echo >&${f} "Synopsis: ${p} [-h]"
  echo >&${f} "          ${p} [:-v:] [-t | -T Sh|sh|Ss|ss  [-c]] [FILE]"
  exit ${ex}
}

##

if ( set -C ) >/dev/null 2>&1; then
  set +C
else
  # For heaven's sake auto-redirect on SunOS/Solaris
  if [ -f /usr/xpg4/bin/sh ] && [ -x /usr/xpg4/bin/sh ]; then
    exec /usr/xpg4/bin/sh "${0}" "${@}"
  else
    synopsis 1 'sh(1)ell without "set -C" (for safe temporary file creation)'
  fi
fi

find_awk || synopsis 1 'Cannot find a usable awk(1) implementation'

while getopts hvtT:c i; do
  case ${i} in
  h)
    synopsis ${EX_OK};;
  v)
    V=`expr ${V} + 1`;;
  t)
    [ x != x"${T}" ] && synopsis ${EX_USAGE} '-toc line expansion yet defined'
    T=Sh;;
  T)
    [ x != x"${T}" ] && synopsis ${EX_USAGE} '-toc line expansion yet defined'
    case "${OPTARG}" in
    [Ss]h)  T=Sh;;
    [Ss]s)  T=Ss;;
    *)      synopsis ${EX_USAGE} "Invalid -T argument: -- ${OPTARG}";;
    esac;;
  c)
    TT=-compact;;
  ?)
    synopsis ${EX_USAGE} '';;
  esac
done
[ -n "${TT}" ] && [ -z "${T}" ] && synopsis ${EX_USAGE} '-c requires -t or -T'
i=`expr ${OPTIND} - 1`
[ ${i} -gt 0 ] && shift ${i}

[ ${#} -gt 1 ] && synopsis ${EX_USAGE} 'Excess arguments given'
[ ${#} -eq 0 ] && F=- || F=${1}

##

# awk(1) does not support signal handlers, so when we are part of a pipe which
# the user terminates we are not capable to deal with the broken pipe case that
# our END{} handler will generate when we had to perform any preprocessing, and
# that in turn would result in a dangling temporary file!
# Thus the only sane option seems to be to always create the temporary file,
# whether we need it or not, not to exec(1) awk(1) but keep on running shell in
# order to remove the temporary after awk(1) has finished, whichever way.

find_tmpdir() {
  i=${IFS}
  IFS=:
  set -- ${ENV_TMP}
  IFS=${i}
  # for i; do -- new in POSIX Issue 7 + TC1
  for tmpdir
  do
    [ -d "${tmpdir}" ] && return 0
  done
  tmpdir=${TMPDIR}
  [ -d "${tmpdir}" ] && return 0
  echo >&2 'Cannot find a usable temporary directory, please set $TMPDIR'
  exit ${EX_TEMPFAIL}
}
find_tmpdir

max=421
[ ${V} -gt 1 ] && max=3
i=1
# RW by user only, avoid overwriting of existing files
old_umask=`umask`
umask 077
while [ 1 ]; do
  tmpfile="${tmpdir}/mdocmx-${i}.mx"
  (
    set -C
    : > "${tmpfile}"
  ) >/dev/null 2>&1 && break
  i=`expr ${i} + 1`
  if [ ${i} -gt ${max} ]; then
    echo >&2 'Cannot create a temporary file within '"${tmpdir}"
    exit ${EX_TEMPFAIL}
  fi
done
trap "exit ${EX_TEMPFAIL}" HUP INT QUIT PIPE TERM
trap "trap \"\" HUP INT QUIT PIPE TERM EXIT; rm -f ${tmpfile}" EXIT
umask ${old_umask}

# Let's go awk(1) {{{
APOSTROPHE=\'
${AWK} -v VERBOSE=${V} -v TOC="${T}" -v TOCTYPE="${TT}" -v MX_FO="${tmpfile}" \
  -v EX_USAGE="${EX_USAGE}" -v EX_DATAERR="${EX_DATAERR}" \
'BEGIN{
  # The mdoc macros that support referenceable anchors.
  # .Sh and .Ss also create anchors, but since they do not require .Mx they are
  # treated special and handled directly -- update manual on change!
  UMACS = "Ar Cd Cm Dv Er Ev Fl Fn Fo Ic In Pa Va Vt"

  # Some of those impose special rules for their arguments; mdocmx(1) solves
  # this by outsourcing such desires in argument parse preparation hooks
  UMACS_KEYHOOKS = "Fl Fn"

  # A list of all mdoc commands; taken from mdocml, "mdoc.c,v 1.226 2014/10/16"
  UCOMMS = \
      "%A %B %C %D %I %J %N %O %P %Q %R %T %U %V " \
      "Ac Ad An Ao Ap Aq Ar At Bc Bd " \
        "Bf Bk Bl Bo Bq Brc Bro Brq Bsx Bt Bx " \
      "Cd Cm D1 Db Dc Dd Dl Do Dq Dt Dv Dx " \
      "Ec Ed Ef Ek El Em En Eo Er Es Ev Ex Fa Fc Fd Fl Fn Fo Fr Ft Fx " \
      "Hf " \
      "Ic In It " \
      "Lb Li Lk Lp " \
      "Ms Mt Nd Nm No Ns Nx " \
      "Oc Oo Op Os Ot Ox Pa Pc Pf Po Pp Pq " \
      "Qc Ql Qo Qq Re Rs Rv " \
      "Sc Sh Sm So Sq Ss St Sx Sy Ta Tn " \
      "Ud Ux Va Vt Xc Xo Xr " \
      "br ll sp"

  # Punctuation to be ignored (without changing current mode)
  UPUNCTS = ". , : ; ( ) [ ] ? !"

  #  --  >8  --  8<  --  #

  i = split(UMACS, savea)
  for(j = 1; j <= i; ++j){
    k = savea[j]
    MACS[k] = k
  }

  i = split(UMACS_KEYHOOKS, savea)
  for(j = 1; j <= i; ++j){
    k = savea[j]
    MACS_KEYHOOKS[k] = k
  }

  i = split(UCOMMS, savea)
  for(j = 1; j <= i; ++j){
    k = savea[j]
    COMMS[k] = k
  }

  i = split(UPUNCTS, savea)
  for(j = 1; j <= i; ++j){
    k = savea[j]
    PUNCTS[k] = k
  }

  mx_bypass = 0   # No work if parsing already preprocessed file!

  mx_nlcont = ""  # Line continuation in progress?  (Then: data so far)
  mx_nlcontfun = 0 # Which function to call once line complete
  NLCONT_SH_SS_COMM = 1
  NLCONT_MX_COMM = 2
  NLCONT_MX_CHECK_LINE = 3

  #mx_sh[]        # Arrays which store headlines, and their sizes
  #mx_sh_cnt
  #mx_sh_toc      # Special avoidance of multiple TOC anchors needed, ++
  #mx_ss[]
  #mx_ss_cnt
  #mx_sh_ss[]     # With TOC we need relation of .Ss with its .Sh
  #mx_fo = ""     # Our temporary output fork (cleaned of .Mx)
  #mx_macros[]    # Readily prepared anchors: macros..
  #mx_keys[]      # ..: keys
  #mx_anchors_cnt # ..number of anchors
  #mx_stack[]     # Stack of future anchors to be parsed off..
  #mx_stack_cnt   # ..number thereof
  #mx_keystack[]  # User specified ".Mx MACRO KEY": store KEY somewhere
  #ARG, [..]      # Next parsed argument (from arg_parse() helper)
}

END{
  # If we were forced to create referenceable anchors, dump the temporary file
  # after writing our table-of-anchors (TAO :)
  if(mx_fo){
    close(mx_fo)

    if(mx_stack_cnt > 0)
      warn("At end of file: \".Mx\" stack not empty (" mx_stack_cnt " levels)")

    for(i = 1; i <= mx_sh_cnt; ++i){
      printf ".Mx -anchor-spass Sh \"%s\" %d\n", arg_quote(mx_sh[i]), i
      for(j = 1; j <= mx_ss_cnt; ++j)
        if(mx_sh_ss[j] == i)
          printf ".Mx -anchor-spass Ss \"%s\" %d\n",
            arg_quote(mx_ss[j]), mx_sh_ss[j]
    }

    for(i = 1; i <= mx_anchors_cnt; ++i)
      printf ".Mx -anchor-spass %s \"%s\"\n",
        mx_macros[i], arg_quote(mx_keys[i])

    # If we are about to produce a TOC, intercept ".Mx -toc" lines and replace
    # them with the desired TOC content
    if(!TOC){
      while((getline < mx_fo) > 0)
        print
    }else{
      while((getline < mx_fo) > 0){
        if($0 ~ /^[[:space:]]*\.[[:space:]]*Mx[[:space:]]+-toc[[:space:]]*/){
          print ".Sh \"\\*[mx-toc-name]\""
          if(mx_sh_cnt > 0){
            print ".Bl -inset", TOCTYPE
            for(i = 1; i <= mx_sh_cnt; ++i){
              printf ".It Sx \"%s\"\n", arg_quote(mx_sh[i])
              if(TOC == "Ss")
                toc_print_ss(i)
            }
            print ".El"
          }
          # Rather illegal, but maybe we have seen .Ss yet no .Sh: go!
          else if(TOC == "Ss" && mx_ss_cnt > 0){
            print ".Bl -tag -compact"
            for(i = 1; i <= mx_ss_cnt; ++i)
              print ".It Sx \"%s\"\n", arg_quote(mx_ss[i])
            print ".El"
          }
        }else
          print
      }
    }
  }
}

function f_a_l(){
  if(!fal){
    fal = FILENAME
    if(!fal || fal == "-")
      fal = "<stdin>"
  }
  return fal ":" NR
}

function dbg(s){
  if(VERBOSE > 1)
    print "DBG@" f_a_l() ": " s > "/dev/stderr"
}

function warn(s){
  if(VERBOSE > 0)
    print "WARN@" f_a_l() ": mdocmx(7): " s "." > "/dev/stderr"
}

function fatal(e, s){
  print "FATAL@" f_a_l() ": mdocmx(7): " s "." > "/dev/stderr"
  exit e
}

# Dump all .Ss which belong to the .Sh with the index sh_idx, if any
function toc_print_ss(sh_idx){
  tps_any = 0
  for(tps_i = 1; tps_i <= mx_ss_cnt; ++tps_i){
    tps_j = mx_sh_ss[tps_i]
    if(tps_j < sh_idx)
      continue
    if(tps_j > sh_idx)
      break

    if(!tps_any){
      tps_any = 1
      print ".Bl -tag -offset indent -compact"
    }
    printf ".It Sx \"%s\"\n", arg_quote(mx_ss[tps_i])
  }
  if(tps_any)
    print ".El"
}

# Parse the next _roff_ argument from the awk(1) line (in $0).
# If "no" < 0, reset the parser and return whether the former state would
# have parsed another argument from the line.
# If "no" is >0 we start at $(no); if it is 0, iterate to the next argument.
# Returns ARG.  Only used when "hot"
function arg_pushback(arg){ ap_pushback = arg }
function arg_parse(no){
  if(no < 0){
    no = ap_no
    ap_no = 0
    ap_pushback = ""
    return no < NF
  }
  if(no == 0){
    if(ap_pushback){
      ARG = ap_pushback
      ap_pushback = ""
      return ARG
    }
    no = ap_no + 1
  }
  ap_pushback = ""

  ARG = ""
  for(ap_i = 0; no <= NF; ++no){
    ap_j = $(no)

    # The good news about quotation mode is that entering it requires
    # a preceeding space: we get it almost for free with awk(1)!
    if(!ap_i){
      if(ap_j ~ /^"/){
        ap_i = 1
        ap_j = substr(ap_j, 2)
      }else{
        ARG = ap_j
        break
      }
    }else
      ARG = ARG " "

    if((ap_k = index(ap_j, "\"")) != 0){
      do{
        # The bad news on quotation mode are:
        # - "" inside it resolves to a single "
        # - " need not mark EOS, but any " that is not followed by "
        #   ends quotation mode and marks the beginning of the next arg
        # - awk(1) has no goto;
        if(ap_k == length(ap_j)){
          ARG = ARG substr(ap_j, 1, ap_k - 1)
          ap_no = no
          ap_i = 0
          return ARG
        }else if(substr(ap_j, ap_k + 1, 1) == "\""){
          ARG = ARG substr(ap_j, 1, ap_k)
          ap_j = substr(ap_j, ap_k + 2)
        }else{
          ARG = ARG substr(ap_j, 1, ap_k)
          ap_j = substr(ap_j, ap_k + 1)
          $(no) = ap_j
          ap_no = no
          ap_i = 0
          return ARG
        }
      }while((ap_k = index(ap_j, "\"")) > 0)
    }else
      ARG = ARG ap_j
  }
  ap_no = no
  return ARG
}

function arg_cleanup(arg){
  # Deal with common special glyphs etc.
  # Note: must be in sync with mdocmx(7) macros (mx:cleanup-string)!
  ac_i = match(arg, /([ \t]|\\&|\\%|\\\/|\\c)+$/)
  if(ac_i)
    arg = substr(arg, 1, ac_i - 1)
  while(arg ~ /^[ \t]/)
    arg = substr(arg, 1)
  while(arg ~ /^(\\&|\\%)/ && arg !~ /^\\&\\&/)
    arg = substr(arg, 3)
  return arg
}

function arg_quote(arg){
  aq_a = arg
  gsub("\"", "\"\"", aq_a)
  return aq_a
}

# ".Mx -enable" seen
function mx_enable(){
  # However, are we running on an already preprocessed document?  Bypass!
  if(NF > 2){
    if($3 == "-preprocessed"){
      print
      mx_bypass = 1
      return
    }
  }

  # If we generate the TOC ourselfs better ensure the string mx-toc-name!
  # mdocml.bsd.lv (mandoc(1)) does not offer any ".if !d NAME" way, so..
  # But even otherwise we need it, since mandoc(1) complains about unknown
  # \*[] strings in quoted strings, and we *may* have a ".Mx -toc" anyway!
  if(!TOC)
    printf ".\\\" Uncomment for mandoc(1) compat.:\n.\\\""
  print ".ds mx-toc-name TABLE OF CONTENTS"
  mx_fo = MX_FO
  $1 = $2 = ""
  $0 = substr($0, 2)
  print ".Mx -enable -preprocessed" $0
}

# Deal with a non-"-enable" ".Mx" request
function mx_comm(){
  # No argument: plain push
  if(NF == 1){
    ++mx_stack_cnt
    dbg(".Mx: [noarg] -> +1, stack size=" mx_stack_cnt)
    return
  }

  # ".Mx -disable"
  if($2 == "-disable"){
    # Nothing to do here (and do not check device arguments)
    return
  }

  # ".Mx -ix" / ".Mx -sx" freely definable anchors
  if($2 == "-sx")
    # Nothing to do here (xxx check argument content validity?)
    return
  else if($2 == "-ix"){
    mxc_macro = arg_parse(3)
    if(!mxc_macro)
      fatal(EX_USAGE, "\".Mx -ix\": synopsis: \".Mx -ix [category] key\"")
    if(!(mxc_key = arg_parse(0))){
      mxc_key = mxc_macro
      mxc_macro = "ixsx"
    }else if(arg_parse(-1))
      fatal(EX_DATAERR, "\".Mx -ix\": data after USER KEY is faulty syntax")
    mxc_key = arg_cleanup(mxc_key)
    dbg(".Mx -ix mac<" mxc_macro "> key <" mxc_key ">")
    anchor_add(mxc_macro, mxc_key)
    return
  }

  # ".Mx -toc"
  if($2 == "-toc"){
    # With TOC creation we surely want the TOC to have an anchor, too!
    if(!mx_sh_toc++)
      mx_sh[++mx_sh_cnt] = "\\*[mx-toc-name]"
    else
      warn("\".Mx -toc\": multiple TOCs?  Duplicate anchor avoided")
    return
  }

  # This explicitly specifies the macro to create an anchor for next
  mxc_i = $2
  if(mxc_i ~ /^\./){
    warn("\".Mx\": stripping dot prefix from \"" mxc_i "\"")
    mxc_i = substr(mxc_i, 2)
  }

  mxc_j = MACS[mxc_i]
  if(!mxc_j)
    fatal(EX_DATAERR, "\".Mx\": macro \"" mxc_i "\" not supported")
  mx_stack[++mx_stack_cnt] = mxc_i
  dbg(".Mx: for next \"." mxc_i "\", stack size=" mx_stack_cnt)

  # Do we also have a fixed key?
  if(NF == 2)
    return
  mx_keystack[mx_stack_cnt] = arg_parse(3)
  dbg("  ... USER KEY given: <" ARG ">")
  if(arg_parse(-1))
    fatal(EX_DATAERR, "\".Mx\": data after USER KEY is faulty syntax")
}

# mx_stack_cnt is >0, check whether this line will pop the stack
function mx_check_line(){
  # May be line continuation in the middle of nowhere
  if(!mx_stack_cnt)
    return
  # Must be a non-comment, non-escaped macro line
  if($0 !~ /^[[:space:]]*[.'${APOSTROPHE}'][[:space:]]*[^"#]/)
    return

  # Iterate over all arguments and try to classify them, comparing them against
  # stack content as applicable
  mcl_mac = ""
  mcl_cont = 0
  mcl_firstmac = 1
  for(arg_parse(-1); arg_parse(0);){
    # Solely ignore punctuation (xxx are we too stupid here?)
    if(PUNCTS[ARG])
      continue

    # (xxx Do this at the end of the loop instead, after decrement?)
    if(mx_stack_cnt == 0){
      dbg("stack empty, stopping arg processing before <" ARG ">")
      break
    }

    mcl_j = mx_stack[mx_stack_cnt]

    # Is this something we consider a macro?  For convenience and documentation
    # of roff stuff do auto-ignore a leading dot of the name in question
    mcl_cont = 0
    if(mcl_firstmac && ARG ~ /^\./)
      ARG = substr(ARG, 2)
    mcl_firstmac = 0
    mcl_i = MACS[ARG]
    if(mcl_i)
      mcl_mac = mcl_i
    else{
      if(!mcl_mac)
        continue
      # It may be some mdoc command nonetheless, ensure it does not fool our
      # simpleminded processing, and end possible mcl_mac savings
      if(COMMS[ARG]){
        if(mcl_j)
          dbg("NO POP due macro (got<" ARG "> want<" mcl_j ">)")
        mcl_mac = ""
        continue
      }
      mcl_i = mcl_mac
      mcl_cont = 1
    }

    # Current command matches the one on the stack, if there is any
    if(mcl_j){
      if(mcl_i != mcl_j){
        dbg("NO POP due macro (got<" mcl_i "> want<" mcl_j ">)")
        mcl_mac = ""
        continue
      }
    }

    # We need the KEY
    if(!mcl_cont && !arg_parse(0))
      fatal(EX_DATAERR, "\".Mx\": expected KEY after \"" mcl_mac "\"")
    ARG = arg_cleanup(ARG)
    if(ARG ~ /^\\&\\&/)
      warn("\".Mx\": KEY starting with \"\\&\\&\" will never match: " ARG)
    if(MACS_KEYHOOKS[mcl_mac])
      _mx_check_line_keyhook()

    if(mx_keystack[mx_stack_cnt]){
      mcl_i = mx_keystack[mx_stack_cnt]
      if(mcl_i != ARG){
        dbg("NO POP mac<" mcl_mac "> due key (got<" ARG "> want <" mcl_i ">)")
        continue
      }
      delete mx_keystack[mx_stack_cnt]
      mcl_i = "STACK"
    }else
      mcl_i = "USER"

    delete mx_stack[mx_stack_cnt--]
    dbg("POP mac<" mcl_mac "> " mcl_i " key <" ARG \
      "> stack size=" mx_stack_cnt)

    anchor_add(mcl_mac, ARG)
  }
}

function _mx_check_line_keyhook(){
  # .Fl: arguments may be continued via |, as in ".Fl a | b | c"
  if(mcl_mac == "Fl"){
    mclpkh_i = ARG
    for(mclpkh_j = 0;; ++mclpkh_j){
      if(!arg_parse(0))
        break
      if(ARG != "|"){
        arg_pushback(ARG)
        break
      }
      if(!arg_parse(0)){
        warn("Premature end of \".Fl\" continuation via \"|\"")
        break
      }
      # Be aware that this argument may indeed be a macro
      # XXX However, only support another Fl as in
      # XXX  .Op Fl T | Fl t Ar \&Sh | sh | \&Ss | ss
      # XXX We are too stupid to recursively process any possible thing,
      # XXX more complicated recursions are simply not supported
      if(ARG == "Fl"){
        arg_pushback(ARG)
        break
      }
      ARG = arg_cleanup(ARG)
      mclpkh_i = mclpkh_i " | " ARG
    }
    ARG = mclpkh_i
  }
  # .Fn: in ".Fn const char *funcname" all we want is "funcname"
  else if(mcl_mac == "Fn"){
    if(ARG ~ /[*&[:space:]]/){
      mclpkh_i = match(ARG, /[^*&[:space:]]+$/)
      ARG = arg_cleanup(substr(ARG, mclpkh_i))
    }
  }
}

# Add one -anchor-spass macro/key pair
function anchor_add(macro, key){
  for(aa_i = 1; aa_i <= mx_anchors_cnt; ++aa_i)
    if(mx_macros[aa_i] == macro && mx_keys[aa_i] == key){
      warn("\".Mx\": mac<" macro ">: duplicate anchor avoided: " key)
      return
    }
  ++mx_anchors_cnt
  mx_macros[mx_anchors_cnt] = macro
  mx_keys[mx_anchors_cnt] = key
}

# Handle a .Sh or .Ss
function sh_ss_comm(){
  ssc_s = ""
  ssc_i = 0
  for(arg_parse(-1); arg_parse(0); ++ssc_i){
    if(ssc_i < 1)
      continue
    if(ssc_i > 1)
      ssc_s = ssc_s " "
    ssc_s = ssc_s ARG
  }
  ssc_s = arg_cleanup(ssc_s)
  if($1 ~ /Sh/)
    mx_sh[++mx_sh_cnt] = ssc_s
  else{
    if(mx_sh_cnt == 0)
      fatal(EX_DATAERR, ".Ss at beginning of document not allowed by mdoc(7)")
    mx_ss[++mx_ss_cnt] = ssc_s
    mx_sh_ss[mx_ss_cnt] = mx_sh_cnt
  }
}

# This is our *very* primitive way of dealing with line continuation
function line_nlcont_add(fun){
  mx_nlcont = mx_nlcont $0
  mx_nlcont = substr(mx_nlcont, 1, length(mx_nlcont) - 1)
  if(!mx_nlcontfun)
    mx_nlcontfun = fun
}

function line_nlcont_done(){
  lnd_save = $0
  $0 = mx_nlcont $0
  mx_nlcont = ""
  if(mx_nlcontfun == NLCONT_SH_SS_COMM)
    sh_ss_comm()
  else if(mx_nlcontfun == NLCONT_MX_COMM)
    mx_comm()
  else if(mx_nlcontfun == NLCONT_MX_CHECK_LINE)
    mx_check_line()
  else
    fatal(EX_DATAERR, "mdocmx(1) implementation error: line_nlcont_done()")
  mx_nlcontfun = 0
  $0 = lnd_save # simplify callees life
}

# .Mx is a line that we care about
/^[[:space:]]*[.'${APOSTROPHE}'][[:space:]]*M[Xx][[:space:]]*/{
  if(mx_bypass)
    print
  else if(mx_fo){
    if(NF > 1 && $2 == "-enable")
      fatal(EX_USAGE, "\".Mx -enable\" may be used only once")
    if(mx_nlcont)
      fatal(EX_DATAERR, "Line continuation too complicated for mdocmx(1)")
    if($0 ~ /\\$/)
      line_nlcont_add(NLCONT_MX_COMM)
    else
      mx_comm()
    print >> mx_fo
  }else if(NF < 2 || $2 != "-enable")
    fatal(EX_USAGE, "\".Mx -enable\" must be the first \".Mx\" command")
  else
    mx_enable()
  next
}

# .Sh and .Ss are also lines we care about, but always store the data in
# main memory, since those commands occur in each mdoc file
/^[[:space:]]*[.'${APOSTROPHE}'][[:space:]]*S[hs][[:space:]]+/{
  if(mx_fo){
    if(mx_nlcont)
      fatal(EX_DATAERR, "Line continuation too complicated for mdocmx(1)")
    if($0 ~ /\\$/)
      line_nlcont_add(NLCONT_SH_SS_COMM)
    else
      sh_ss_comm()
    print >> mx_fo
    next
  }
}

# All other lines are uninteresting unless mdocmx is -enabled and we have
# pending anchor creation requests on the stack
{
  if(!mx_fo)
    print
  else{
    # TODO No support for any macro END but ..
    if(/^[[:space:]]*[.'${APOSTROPHE}'][[:space:]]*dei?1?[[:space:]]+/){
      if(mx_nlcont)
        fatal(EX_DATAERR, "Line continuation too complicated for mdocmx(1)")
      print >> mx_fo
      while(getline > 0 && $0 !~ /^\.\.$/)
        print >> mx_fo
    }else if($0 ~ /\\$/)
      line_nlcont_add(NLCONT_MX_CHECK_LINE)
    else if(mx_nlcont)
      line_nlcont_done()
    else if(mx_stack_cnt)
      mx_check_line()
    else if(/^[[:space:]]*\.(\\"|[[:space:]]*$)/)
      next
    print >> mx_fo
  }
}' "${F}"
# }}}

exit
# s-it2-mode
