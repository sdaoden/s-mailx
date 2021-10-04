#!/bin/sh -
#@ Either create src/su/gen-errors.h, or, at compile time, the OS<>SU map.
#
# Public Domain

IN="${SRCDIR}"su/gen-errors.h
XOUT=src/su/gen-errors.h

# We use `csop' for hashing
MAILX='LC_ALL=C s-nail -#:/'

# Acceptable "longest distance" from hash-modulo-index to key
MAXDISTANCE_PENALTY=6

# Generate a more verbose output.  Not for shipout versions.
VERB=1

##

LC_ALL=C
export LC_ALL MAXDISTANCE_PENALTY VERB MAILX IN XOUT

: ${awk:=awk}
# Compile-time only
: ${rm:=rm}
: ${sort:=sort}

# The set of errors we support {{{
ERRORS="\
   NONE='No error' \
   2BIG='Argument list too long' \
   ACCES='Permission denied' \
   ADDRINUSE='Address already in use' \
   ADDRNOTAVAIL='Cannot assign requested address' \
   AFNOSUPPORT='Address family not supported by protocol family' \
   AGAIN='Resource temporarily unavailable' \
   ALREADY='Operation already in progress' \
   BADF='Bad file descriptor' \
   BADMSG='Bad message' \
   BUSY='Device busy' \
   CANCELED='Operation canceled' \
   CHILD='No child processes' \
   CONNABORTED='Software caused connection abort' \
   CONNREFUSED='Connection refused' \
   CONNRESET='Connection reset by peer' \
   DEADLK='Resource deadlock avoided' \
   DESTADDRREQ='Destination address required' \
   DOM='Numerical argument out of domain' \
   DQUOT='Disc quota exceeded' \
   EXIST='File exists' \
   FAULT='Bad address' \
   FBIG='File too large' \
   HOSTUNREACH='No route to host' \
   IDRM='Identifier removed' \
   ILSEQ='Illegal byte sequence' \
   INPROGRESS='Operation now in progress' \
   INTR='Interrupted system call' \
   INVAL='Invalid argument' \
   IO='Input/output error' \
   ISCONN='Socket is already connected' \
   ISDIR='Is a directory' \
   LOOP='Too many levels of symbolic links' \
   MFILE='Too many open files' \
   MLINK='Too many links' \
   MSGSIZE='Message too long' \
   MULTIHOP='Multihop attempted' \
   NAMETOOLONG='File name too long' \
   NETDOWN='Network is down' \
   NETRESET='Network dropped connection on reset' \
   NETUNREACH='Network is unreachable' \
   NFILE='Too many open files in system' \
   NOBUFS='No buffer space available' \
   NODATA='No data available' \
   NODEV='Operation not supported by device' \
   NOENT='No such entry, file or directory' \
   NOEXEC='Exec format error' \
   NOLCK='No locks available' \
   NOLINK='Link has been severed' \
   NOMEM='Cannot allocate memory' \
   NOMSG='No message of desired type' \
   NOPROTOOPT='Protocol not available' \
   NOSPC='No space left on device' \
   NOSR='Out of streams resource' \
   NOSTR='Device not a stream' \
   NOSYS='Function not implemented' \
   NOTCONN='Socket is not connected' \
   NOTDIR='Not a directory' \
   NOTEMPTY='Directory not empty' \
   NOTOBACCO='No tobacco, snorkeling on empty pipe' \
   NOTRECOVERABLE='State not recoverable' \
   NOTSOCK='Socket operation on non-socket' \
   NOTSUP='Operation not supported' \
   NOTTY='Inappropriate ioctl for device' \
   NXIO='Device not configured' \
   OPNOTSUPP='Operation not supported' \
   OVERFLOW='Value too large to be stored in data type' \
   OWNERDEAD='Previous owner died' \
   PERM='Operation not permitted' \
   PIPE='Broken pipe' \
   PROTO='Protocol error' \
   PROTONOSUPPORT='Protocol not supported' \
   PROTOTYPE='Protocol wrong type for socket' \
   RANGE='Result too large' \
   ROFS='Read-only filesystem' \
   SPIPE='Invalid seek' \
   SRCH='No such process' \
   STALE='Stale NFS file handle' \
   TIME='Timer expired' \
   TIMEDOUT='Operation timed out' \
   TXTBSY='Text file busy' \
   WOULDBLOCK='Operation would block' \
   XDEV='Cross-device link' \
"
export ERRORS
# }}}

error_parse() {
   j=\'
   ${awk} -v dodoc="${1}" -v incnone="${2}" -v input="${ERRORS}" '
      BEGIN{
         for(i = 0;;){
            voff = match(input, /[0-9a-zA-Z_]+(='${j}'[^'${j}']+)?/)
            if(voff == 0)
               break
            v = substr(input, voff, RLENGTH)
            input = substr(input, voff + RLENGTH)
            doff = index(v, "=")
            if(doff > 0){
               d = substr(v, doff + 2, length(v) - doff - 1)
               v = substr(v, 1, doff - 1)
            }
            if(!incnone && (v == "NONE" || v == "NOTOBACCO"))
               continue
            print dodoc ? d : v
         }
      }
   '
}

compile_time() { # {{{
   [ -n "${TARGET}" ] || {
      echo >&2 'Invalid usage'
      exit 1
   }
   set -e

   {
      printf '#include <errno.h>\nsu_ERROR_START\n'
      for n in `error_parse 0 0`; do
         printf '#ifdef E%s\nE%s "%s"\n#else\n-1 "%s"\n#endif\n' $n $n $n $n
      done
   } > "${TARGET}".c

   # The problem is that at least (some versions of) gcc mangle output.
   # Ensure we get both arguments on one line.
   # While here sort numerically.
   "${CC}" -E "${TARGET}".c |
      ${awk} '
         function stripsym(sym){
            sym = substr(sym, 2)
            sym = substr(sym, 1, length(sym) - 1)
            return sym
         }
         BEGIN{hot=0; conti=0}
         /^[ 	]*$/{next}
         /^[ 	]*#/{next}
         /^su_ERROR_START$/{hot=1; next}
         {
            if(!hot)
               next
            i = conti ? stripsym($1) "\n" : $1 " "
            printf i
            if(conti)
               conti = 0
            else if($2 != "")
               printf "%s\n", stripsym($2)
            else
               conti = 1
         }
      ' |
      ${sort} -n > "${TARGET}".txt

   # EBCDIC/ASCII: we use \134 for reverse solidus \
   j=\'
   ${awk} -v verb="${VERB}" -v input="${ERRORS}" -v dat="${TARGET}.txt" '
      BEGIN{
         verb = (verb != 0) ? "   " : ""

         # Read in our OS data

         unavail = 0
         max = 0
         oscnt = 0
         while((getline dl < dat) > 0){
            split(dl, ia)

            ++oscnt
            osnoa[oscnt] = ia[1]
            osnaa[oscnt] = ia[2]

            if(ia[1] < 0)
               ++unavail
            else{
               if(ia[1] > max)
                  max = ia[1]
            }
         }
         close(dat)

         # Maximum error number defines the datatype to use.
         # We need a value for NOTOBACCO, we warp all non-available errors to
         # numbers too high to be regular errors, counting backwards

         i = max + unavail + 1
         if(i >= 65535){
            t = "u32"
            max = "0xFFFFFFFFu"
         }else if(i >= 255){
            t = "u16"
            max = "0xFFFFu"
         }else{
            t = "u8"
            max = "0xFFu"
         }
         print "#define su__ERR_NUMBER_TYPE " t
         print "#define su__ERR_NUMBER_MAX " max

         # Dump C table

         cnt = 0
         print "#define su__ERR_NUMBER_ENUM_C \134"

         print verb "su_ERR_NONE = 0,\134"
         ++cnt

         # Since our final table is searched with binary sort,
         # place the non-available backward counting once last
         unavail = j = k = 0
         for(i = 1; i <= oscnt; ++i){
            if(osnoa[i] >= 0){
               map[osnaa[i]] = osnoa[i]
               print verb "su_ERR_" osnaa[i] " = " osnoa[i] ",\134"
               ++cnt
            }else{
               ++unavail
               the_unavail[unavail] = "su_ERR_" osnaa[i] " = " \
                     "(su__ERR_NUMBER_MAX - " unavail ")"
            }
         }
         for(i = unavail; i >= 1; --i){
            print verb the_unavail[i] ",\134"
            ++cnt
         }

         print verb "su_ERR_NOTOBACCO = su__ERR_NUMBER_MAX,\134"
         ++cnt

         print verb "su__ERR_NUMBER = " cnt

         # The C++ mapping table

         print "#ifdef __cplusplus"
         print "# define su__CXX_ERR_NUMBER_ENUM \134"
         print verb "enone = su_ERR_NONE,\134"

         unavail = 0
         for(i = 1; i <= oscnt; ++i){
            if(osnoa[i] >= 0)
               print verb "e" tolower(osnaa[i]) " = su_ERR_" osnaa[i] ",\134"
            else{
               ++unavail
               the_unavail[unavail] = "e" tolower(osnaa[i]) " = su_ERR_" \
                     osnaa[i]
            }
         }
         for(i = unavail; i >= 1; --i){
            print verb the_unavail[i] ",\134"
            ++cnt
         }
         print verb "enotobacco = su_ERR_NOTOBACCO,\134"
         print verb "e__number = su__ERR_NUMBER"

         print "#endif /* __cplusplus */"

         # And our OS errno -> name map offset table
         # (This "OS" however includes the unavail ones)

         voidoff = 0
         for(mapoff = 0;; ++mapoff){
            voff = match(input, /[0-9a-zA-Z_]+(='${j}'[^'${j}']+)?/)
            if(voff == 0)
               break

            v = substr(input, voff, RLENGTH)
            input = substr(input, voff + RLENGTH)
            doff = index(v, "=")
            if(doff > 0){
               d = substr(v, doff + 2, length(v) - doff - 1)
               v = substr(v, 1, doff - 1)
            }
            mapo[v] = mapoff
            if(v == "NOTOBACCO")
               voidoff = mapoff
         }

         uniq = 0
         print "#define su__ERR_NUMBER_VOIDOFF " voidoff
         print "#define su__ERR_NUMBER_TO_MAPOFF \134"

         print verb "a_X(0, 0) \134"
         ++uniq

         unavail = 0
         mapdups[0] = 1
         for(i = 1; i <= oscnt; ++i){
            if(osnoa[i] < 0){
               the_unavail[++unavail] = i
               continue
            }
            if(mapdups[osnoa[i]])
               continue
            mapdups[osnoa[i]] = 1
            print verb "a_X(" osnoa[i] ", " mapo[osnaa[i]] ") \134"
            ++uniq
         }

         for(i = unavail; i >= 1; --i){
            print verb "a_X(" "su__ERR_NUMBER_MAX - " i ", " \
                  mapo[osnaa[the_unavail[i]]] ")\134"
            ++uniq
         }

         print verb "a_X(su__ERR_NUMBER_MAX, su__ERR_NUMBER_VOIDOFF) \134"
         ++uniq
         print verb "/* " uniq " unique members */"
      }
   ' >> "${TARGET}"

   ${rm} "${TARGET}".*
   exit 0
} # }}}

if [ ${#} -ne 0 ]; then
   if [ "${1}" = noverbose ]; then
      shift
      VERB=0
      export VERB
   fi
fi

if [ ${#} -eq 1 ]; then
   [ "${1}" = compile_time ] && compile_time
elif [ ${#} -eq 0 ]; then
   # Now start perl(1) without PERL5OPT set to avoid multibyte sequence errors
   PERL5OPT= PERL5LIB= exec perl -x "${0}"
fi
echo >&2 'Invalid usage'
exit 1

# PERL {{{
# Thanks to perl(5) and it's -x / #! perl / __END__ mechanism!
# Why can env(1) not be used for such easy things in #!?
#!perl

#use diagnostics -verbose;
use strict;
use warnings;

use FileHandle;
use IPC::Open2;

use sigtrap qw(handler cleanup normal-signals);

my ($S, @ENTS, $CTOOL, $CTOOL_EXE) = ($ENV{VERB} ? '   ' : '');

sub main_fun{
   create_ents();

   create_c_tool();

   hash_em();

   dump_map(); # Starts output file

   reverser(); # Ends output file

   cleanup(undef);
   exit 0
}

sub cleanup{
   die "$CTOOL_EXE: couldn't unlink: $^E"
      if $CTOOL_EXE && -f $CTOOL_EXE && 1 != unlink $CTOOL_EXE;
   die "$CTOOL: couldn't unlink: $^E"
      if $CTOOL && -f $CTOOL && 1 != unlink $CTOOL;
   die "Terminating due to signal $_[0]" if $_[0]
}

sub basen{
   my $n = $_[0];
   $n =~ s/^(.*\/)?([^\/]+)$/$2/;
   $n
}

sub create_ents{
   my $input = $ENV{ERRORS};
   while($input =~ /([[:alnum:]_]+)='([^']+)'(.*)/){
      $input = $3;
      my %vals;
      $vals{name} = $1;
      $vals{doc} = $2;
      push @ENTS, \%vals
   }
}

sub create_c_tool{
   $CTOOL = './tmp-errors-tool-' . $$ . '.c';
   $CTOOL_EXE = $CTOOL . '.exe';

   die "$CTOOL: open: $^E" unless open F, '>', $CTOOL;
   print F '#define MAX_DISTANCE_PENALTY ', $ENV{MAXDISTANCE_PENALTY}, "\n";
# >>>>>>>>>>>>>>>>>>>
   print F <<'_EOT';
#define __CREATE_ERRORS_SH
#define su_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define su_NELEM(A) (sizeof(A) / sizeof((A)[0]))

#define u32 uint32_t
#define s32 int32_t
#define u16 uint16_t
#define u8 uint8_t

struct a_corerr_map{
   u32 cem_hash;     /* Hash of name */
   u32 cem_nameoff;  /* Into a_corerr_names[] */
   u32 cem_docoff;   /* Into a_corerr_docs[] */
   s32 cem_errno;    /* OS errno value for this one */
};

_EOT

   print F '#include "', $ENV{XOUT}, "\"\n\n";

   print F <<'_EOT';
static u8 seen_wraparound;
static size_t longest_distance;

static size_t
next_prime(size_t no){ /* blush (brute force) */
jredo:
   ++no;
   for(size_t i = 3; i < no; i += 2)
      if(no % i == 0)
         goto jredo;
   return no;
}

static size_t *
reversy(size_t size){
   struct a_corerr_map const *cemp = a_corerr_map,
      *cemaxp = cemp + su_NELEM(a_corerr_map);
   size_t ldist = 0, *arr;

   arr = (size_t*)malloc(sizeof *arr * size);
   for(size_t i = 0; i < size; ++i)
      arr[i] = su_NELEM(a_corerr_map);

   seen_wraparound = 0;
   longest_distance = 0;

   while(cemp < cemaxp){
      u32 hash = cemp->cem_hash, i = hash % size, l;

      for(l = 0; arr[i] != su_NELEM(a_corerr_map); ++l)
         if(++i == size){
            seen_wraparound = 1;
            i = 0;
         }
      if(l > longest_distance)
         longest_distance = l;
      arr[i] = (size_t)(cemp++ - a_corerr_map);
   }
   return arr;
}

int
main(int argc, char **argv){
   size_t *arr, size = su_NELEM(a_corerr_map);

   fprintf(stderr, "Starting reversy, okeys=%zu\n", size);
   for(;;){
      arr = reversy(size = next_prime(size));
      fprintf(stderr, " - size=%zu longest_distance=%zu seen_wraparound=%d\n",
         size, longest_distance, seen_wraparound);
      if(longest_distance <= MAX_DISTANCE_PENALTY)
         break;
      free(arr);
   }

   printf(
      "#ifdef su_SOURCE /* Lock-out compile-time-tools */\n"
      "# define a_CORERR_REV_ILL %zuu\n"
      "# define a_CORERR_REV_PRIME %zuu\n"
      "# define a_CORERR_REV_LONGEST %zuu\n"
      "# define a_CORERR_REV_WRAPAROUND %d\n"
      "static %s const a_corerr_revmap[a_CORERR_REV_PRIME] = {\n%s",
      su_NELEM(a_corerr_map), size, longest_distance, seen_wraparound,
      argv[1], (argc > 2 ? "   " : ""));
   for(size_t i = 0; i < size; ++i)
      printf("%s%zuu", (i == 0 ? ""
         : (i % 10 == 0 ? (argc > 2 ? ",\n   " : ",\n")
            : (argc > 2 ? ", " : ","))),
         arr[i]);
   printf("\n};\n#endif /* su_SOURCE */\n");
   return 0;
}
_EOT
# <<<<<<<<<<<<<<<<<<<
   close F
}

sub hash_em{
   die "hash_em: open: $^E"
      unless my $pid = open2 *RFD, *WFD, $ENV{MAILX};
   foreach my $e (@ENTS){
      print WFD "csop hash32?case $e->{name}\n";
      my $h = <RFD>;
      chomp $h;
      $e->{hash} = $h
   }
   print WFD "x\n";
   waitpid $pid, 0;
}

sub dump_map{
   my ($i, $alen);

   die "$ENV{XOUT}: open: $^E" unless open F, '>', $ENV{XOUT};
   print F '/*@ ', scalar basen($ENV{XOUT}), ', generated by ',
      scalar basen($0), ".\n *@ See core-errors.c for more */\n\n";

   ($i, $alen) = (0, 0);
   print F '#ifdef su_SOURCE', "\n",
      'static char const a_corerr_names[] = {', "\n";
   ($i, $alen) = (0, 0);
   foreach my $e (@ENTS){
      $e->{nameoff} = $alen;
      my $k = $e->{name};
      my $l = length $k;
      my $a = join '\',\'', split(//, $k);
      my (@fa);
      print F "${S}/* $i. [$alen]+$l $k */\n" if $ENV{VERB};
      print F "${S}'$a','\\0',\n";
      ++$i;
      $alen += $l + 1
   }
   print F '};', "\n\n";

   print F '# ifdef su_HAVE_DOCSTRINGS', "\n";
   print F '#  undef a_X', "\n", '#  define a_X(X)', "\n";
   print F 'static char const a_corerr_docs[] = {', "\n";
   ($i, $alen) = (0, 0);
   foreach my $e (@ENTS){
      $e->{docoff} = $alen;
      my $k = $e->{doc};
      my $l = length $k;
      my $a = join '\',\'', split(//, $k);
      my (@fa);
      print F "${S}/* $i. [$alen]+$l $e->{name} */ ",
         "a_X(N_(\"$e->{doc}\"))\n" if $ENV{VERB};
      print F "${S}'$a','\\0',\n";
      ++$i;
      $alen += $l + 1
   }
   print F '};', "\n", '#  undef a_X',
      "\n# endif /* su_HAVE_DOCSTRINGS */\n\n";

   print F <<_EOT;
# undef a_X
# ifndef __CREATE_ERRORS_SH
#  define a_X(X) X
# else
#  define a_X(X) 0
# endif
static struct a_corerr_map const a_corerr_map[] = {
_EOT
   foreach my $e (@ENTS){
      print F "${S}{$e->{hash}u, $e->{nameoff}u, $e->{docoff}u, ",
         "a_X(su_ERR_$e->{name})},\n"
   }
   print F '};', "\n", '# undef a_X', "\n",
      '#endif /* su_SOURCE */', "\n\n";

   die "$ENV{XOUT}: close: $^E" unless close F
}

sub reverser{
   my $argv2 = $ENV{VERB} ? ' verb' : '';
   system("\$CC -I. -o $CTOOL_EXE $CTOOL");
   my $t = (@ENTS < 0xFF ? 'u8' : (@ENTS < 0xFFFF ? 'u16' : 'u32'));
   `$CTOOL_EXE $t$argv2 >> $ENV{XOUT}`
}

{package main; main_fun()}
# }}}

# s-it-mode
