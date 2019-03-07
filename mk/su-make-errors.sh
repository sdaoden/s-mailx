#!/bin/sh -
#@ Either create src/su/gen-errors.h, or, at compile time, the OS<>SU map.
# Public Domain

IN="${SRCDIR}"su/gen-errors.h
XOUT=src/su/gen-errors.h

# We use `vexpr' for hashing
MAILX='LC_ALL=C s-nail -#:/'

# Acceptable "longest distance" from hash-modulo-index to key
MAXDISTANCE_PENALTY=5

# Generate a more verbose output.  Not for shipout versions.
VERB=1

##

LC_ALL=C
export LC_ALL MAXDISTANCE_PENALTY VERB MAILX IN XOUT

[ -n "${awk}" ] || awk=awk

# The set of errors we support
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
   NOTSOCK='Socket operation on non-socket' \
   NOTSUP='Operation not supported' \
   NOTTY='Inappropriate ioctl for device' \
   NXIO='Device not configured' \
   OPNOTSUPP='Operation not supported' \
   OVERFLOW='Value too large to be stored in data type' \
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

error_parse() {
   j=\'
   ${awk} -v dodoc="${1}" -v incnone="${2}" -v input="${ERRORS}" '
      BEGIN{
         for(i = 0;;){
            voff = match(input, /[[:alnum:]_]+(='${j}'[^'${j}']+)?/)
            if(voff == 0)
               break
            v = substr(input, voff, RLENGTH)
            input = substr(input, voff + RLENGTH)
            doff = index(v, "=")
            if(doff > 0){
               d = substr(v, doff + 2, length(v) - doff - 1)
               v = substr(v, 1, doff - 1)
            }
            if(!incnone && v == "NONE")
               continue
            print dodoc ? d : v
         }
      }
   '
}

config() {
   [ -n "${TARGET}" ] || {
      echo >&2 'Invalid usage'
      exit 1
   }
   # Note this may be ISO C89, so we cannot
   cat <<__EOT__
   #include <ctype.h>
   #include <errno.h>
   #include <limits.h>
   #include <stdio.h>
   #include <stdlib.h>
   #include <string.h>
   #if defined __STDC_VERSION__ && __STDC_VERSION__ + 0 >= 199901L
   # include <stdint.h>
   #else
   # include <inttypes.h>
   #endif
   #include <${IN}>
   #ifdef UINT32_MAX
   typedef uint32_t u32;
   typedef int32_t s32;
   #elif ULONG_MAX == 0xFFFFFFFFu
   typedef unsigned long int u32;
   typedef signed long int s32;
   #else
   typedef unsigned int u32;
   typedef signed int s32;
   #endif
   struct a_in {struct a_in *next; char const *name; s32 no; u32 uno;};
   static int a_sortin(void const *a, void const *b){
      return (*(struct a_in const* const *)a)->uno -
         (*(struct a_in const* const *)b)->uno;
   }
   int main(void){
      struct a_in *head, *tail, *np, **nap;
      u32 maxsub, umax;
      s32 xavail = 0, total = 1, imin = 0, imax = 0, voidoff = 0, i, j;
      FILE *ofp = fopen("${TARGET}", "a");
      if(ofp == NULL){
         fprintf(stderr, "ERROR: cannot open output\n");
         return 1;
      }

      /* Create a list of all errors */
      head = tail = (struct a_in*)malloc(sizeof *head);
      head->next = NULL; head->name = "su_ERR_NONE"; head->no = 0;
__EOT__
   for n in `error_parse 0 0`; do
      cat <<__EOT__
      ++total;
      #ifdef E${n}
      i = E${n};
      #else
      i = --xavail;
      #endif
      if(imin > i) {imin = i;} if(imax < i) {imax = i;}
      np = (struct a_in*)malloc(sizeof *np);
      np->next = NULL; np->name = "su_ERR_${n}"; np->no = i;
      tail->next = np; tail = np;
__EOT__
   done
   cat <<__EOT__
      /* The unsigned type used for storage */

      fputs("#define su__ERR_NUMBER_TYPE ", ofp);
      if((u32)imax <= 0xFFu && (u32)-imin <= 0xFFu){
         fputs("u8\n", ofp);
         maxsub = 0xFFu;
      }else if(((u32)imax <= 0xFFFFu && (u32)-imin <= 0xFFFFu)){
         fputs("u16\n", ofp);
         maxsub = 0xFFFFu;
      }else{
         fputs("u32\n", ofp);
         maxsub = 0xFFFFFFFFu;
      }

      /* Now that we know the storage type, create the unsigned numbers */
      for(umax = 0, np = head; np != NULL; np = np->next){
         if(np->no < 0)
            np->uno = maxsub + np->no + 1;
         else
            np->uno = np->no;
         if(np->uno > umax)
            umax = np->uno;
      }
      if(umax <= (u32)imax){
         fprintf(stderr, "ERROR: errno ranges overlap\n");
         return 1;
      }

      /* Sort this list */

      nap = (struct a_in**)malloc(sizeof(*nap) * (unsigned)total);
      for(i = 0, np = head; np != NULL; ++i, np = np->next)
         nap[i] = np;
      if(i != total){
         fprintf(stderr, "ERROR: implementation error i != total\n");
         return 1;
      }
      qsort(nap, (u32)i, sizeof *nap, &a_sortin);

      /* The enumeration of numbers */

      fputs("#define su__ERR_NUMBER_ENUM_C \\\\\\n", ofp);
      for(i = 0; i < total; ++i)
         fprintf(ofp, "   %s = %lu,\\\\\\n",
            nap[i]->name, (unsigned long)nap[i]->uno);
      fprintf(ofp, "   su__ERR_NUMBER = %ld\\n", (long)total);

      fputs("#ifdef __cplusplus\n# define su__CXX_ERR_NUMBER_ENUM \\\\\\n",
         ofp);
      for(i = 0; i < total; ++i){
         char b[64], *cbp = b;
         char const *cp;
         cp = &nap[i]->name[sizeof("su_") -1];
         *cbp++ = 'e';
         for(cp += sizeof("ERR_") -1; *cp != '\0'; ++cp)
            *cbp++ = tolower(*cp);
         *cbp = '\0';
         fprintf(ofp, "   %s = %s,\\\\\\n", b, nap[i]->name);
      }
      fprintf(ofp,
         "   e__number = su__ERR_NUMBER\\n#endif /* __cplusplus */\n");

      /* The binary search mapping table from OS error value to our internal
       * a_corerr_map[] error description table */
      fprintf(ofp, "#define su__ERR_NUMBER_TO_MAPOFF \\\\\\n");
      for(xavail = 0, i = 0; i < total; ++i){
         if(i == 0 || nap[i]->no != nap[i - 1]->no){
            for(j = 0; a_names_alphasort[j] != NULL; ++j){
               if(!strcmp(&nap[i]->name[sizeof("su_ERR_") -1],
                     a_names_alphasort[j]))
                  break;
            }
            fprintf(ofp, "\ta_X(%lu, %lu) %s%s%s\\\\\\n",
               (unsigned long)nap[i]->uno, (long)(u32)j,
               ((${VERB}) ? "/* " : ""), ((${VERB}) ? nap[i]->name : ""),
                  ((${VERB}) ? " */ " : ""));
            if(!strcmp("su_ERR_NOTOBACCO", nap[i]->name))
               voidoff = j;
            ++xavail;
         }
      }
      fprintf(ofp, "\\t/* %ld unique members */\\n", (long)xavail);
      fprintf(ofp, "#define su__ERR_NUMBER_VOIDOFF %ld\\n", (long)voidoff);
      fclose(ofp);

      while((np = head) != NULL){
         head = np->next;
         free(np);
      }
      free(nap);
      return 0;
   }
__EOT__
   exit 0
}

if [ ${#} -ne 0 ]; then
   if [ "${1}" = noverbose ]; then
      shift
      VERB=0
      export VERB
   fi
fi

if [ ${#} -eq 1 ]; then
   [ "${1}" = config ] && config
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
      print WFD "vexpr hash32 $e->{name}\n";
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

   print F '#ifndef su_SOURCE /* For compile-time tools only */', "\n",
      'static char const * const a_names_alphasort[] = {';
   ($i, $alen) = (0, 0);
   foreach my $e (@ENTS){
      $i = 1 + 3 + length $e->{name};
      if($alen == 0 || $alen + $i > 75){
         print F "\n${S}";
         $alen = length $S
      }else{
         print F ' ';
         ++$i
      }
      $alen += $i;
      print F "\"$e->{name}\","
   }
   print F " NULL\n};\n#endif /* !su_SOURCE */\n\n";

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
