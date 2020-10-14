#!/usr/bin/env perl
require 5.008_001;
use utf8;
#@ Parse 'enum okeys' from nail.h and create gen-okeys.h.
#@ Just like enum okeys it has to create case-insensitive sorted entries!
#
# Public Domain

my $IN = 'include/mx/nail.h';
my $OUT = 'src/mx/gen-okeys.h';

# We use `csop' for hashing
my $MAILX = 'LC_ALL=C s-nail -#:/';

# Acceptable "longest distance" from hash-modulo-index to key
my $MAXDISTANCE_PENALTY = 6;

# Generate a more verbose output.  Not for shipout versions.
my $VERB = 1;

##  --  >8  --  8<  --  ##

use diagnostics -verbose;
use strict;
use warnings;

use FileHandle;
use IPC::Open2;

use sigtrap qw(handler cleanup normal-signals);

my ($S, @ENTS, $CTOOL, $CTOOL_EXE) = ($VERB ? '   ' : '');

sub main_fun{
   if(@ARGV) {$VERB = 0; $S = ''}

   parse_in_h();

   create_c_tool();

   hash_em();

   dump_map();

   reverser();

   cleanup(undef);
   exit 0
}

sub cleanup{
   die "$CTOOL_EXE: couldn't unlink: $^E"
      if $CTOOL_EXE && -f $CTOOL_EXE && 1 != unlink $CTOOL_EXE;
   die "$CTOOL: couldn't unlink: $^E"
      if $CTOOL && -f $CTOOL && 1 != unlink $CTOOL;
   die "Terminating due to signal $_[0]" if $_[0]
};

sub basen{
   my $n = $_[0];
   $n =~ s/^(.*\/)?([^\/]+)$/$2/;
   $n
}

sub parse_in_h{
   die "$IN: open: $^E" unless open F, '<', $IN;
   my ($init) = (0);
   while(<F>){
      # Only want the enum okeys content
      if(/^enum okeys/) {$init = 1; next}
      if(/^};/) {if($init) {$init = 2; last}; next}
      $init || next;

      # Ignore empty and comment lines
      /^$/ && next;
      /^\s*\/\*/ && next;

      # An entry may have a comment with special directives
      /^\s*(\w+),?\s*(?:\/\*\s*(?:{(.*)})\s*\*\/\s*)?$/;
      next unless $1;
      my ($k, $x) = ($1, $2);
      my %vals;
      $vals{enum} = $k;
      $vals{bool} = ($k =~ /^ok_b/ ? 1 : 0);
      $k = $1 if $k =~ /^ok_[bv]_(.+)$/;
      $k =~ s/_/-/g;
      $vals{name} = $k;
      if($x){
         # {\}: overlong entry, placed on follow line
         if($x =~ /\s*\\\s*$/){
            $_ = <F>;
            die "$IN: missing continuation line" unless $_;
            /^\s*\/\*\s*{(.*)}\s*\*\/\s*$/;
            $x = $1;
            die "$IN: invalid continuation line" unless $x
         }

         while($x && $x =~ /^([^,]+?)(?:,(.*))?$/){
            $x = $2;
            $1 =~ /([^=]+)=(.+)/;
            die "Unsupported special directive: $1"
               if($1 ne 'name' &&
                  $1 ne 'virt' && $1 ne 'vip' &&
                  $1 ne 'rdonly' && $1 ne 'nodel' &&
                  $1 ne 'i3val' && $1 ne 'defval' &&
                  $1 ne 'import' && $1 ne 'env' && $1 ne 'nolopts' &&
                  $1 ne 'notempty' && $1 ne 'nocntrls' &&
                     $1 ne 'num' && $1 ne 'posnum' && $1 ne 'lower' &&
                  $1 ne 'chain' && $1 ne 'obsolete');
            $vals{$1} = $2
         }
      }
      push @ENTS, \%vals
   }
   die 'I do not see the expected content' unless $init == 2;
   close F
}

sub create_c_tool{
   $CTOOL = './tmp-okey-tool-' . $$ . '.c';
   $CTOOL_EXE = $CTOOL . '.exe';

   die "$CTOOL: open: $^E" unless open F, '>', $CTOOL;
   print F '#define MAX_DISTANCE_PENALTY ', $MAXDISTANCE_PENALTY, "\n";
# >>>>>>>>>>>>>>>>>>>
   print F <<'_EOT';
#define a__CREATE_OKEY_MAP_PL
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define NELEM(A) (sizeof(A) / sizeof(A[0]))
#define BITENUM_IS(P,E) P

#define u32 uint32_t
#define u16 uint16_t
#define u8 uint8_t

enum a_amv_var_flags{
   a_AMV_VF_NONE = 0,

   /* The basic set of flags, also present in struct a_amv_var_map.avm_flags */
   a_AMV_VF_BOOL = 1u<<0, /* ok_b_* */
   a_AMV_VF_CHAIN = 1u<<1, /* Has -HOST and/or -USER@HOST variants */
   a_AMV_VF_VIRT = 1u<<2, /* "Stateless" automatic variable */
   a_AMV_VF_VIP = 1u<<3, /* Wants _var_check_vips() evaluation */
   a_AMV_VF_RDONLY = 1u<<4, /* May not be set by user */
   a_AMV_VF_NODEL = 1u<<5, /* May not be deleted */
   a_AMV_VF_I3VAL = 1u<<6, /* Has an initial value */
   a_AMV_VF_DEFVAL = 1u<<7, /* Has a default value */
   a_AMV_VF_IMPORT = 1u<<8, /* Import ONLY from env (pre n_PSO_STARTED) */
   a_AMV_VF_ENV = 1u<<9, /* Update environment on change */
   a_AMV_VF_NOLOPTS = 1u<<10, /* May not be tracked by `localopts' */
   a_AMV_VF_NOTEMPTY = 1u<<11, /* May not be assigned an empty value */
   /* TODO _VF_NUM, _VF_POSNUM: we also need 64-bit limit numbers! */
   a_AMV_VF_NUM = 1u<<12, /* Value must be a 32-bit number */
   a_AMV_VF_POSNUM = 1u<<13, /* Value must be positive 32-bit number */
   a_AMV_VF_LOWER = 1u<<14, /* Values will be stored in lowercase version */
   a_AMV_VF_OBSOLETE = 1u<<15, /* Is obsolete? */
   a_AMV_VF__MASK = (1u<<(15+1)) - 1,

   /* Extended flags, not part of struct a_amv_var_map.avm_flags */
   /* Indicates the instance is actually a variant of a _VF_CHAIN, it thus uses
    * the a_amv_var_map of the base variable, but it is not the base itself and
    * therefore care must be taken */
   a_AMV_VF_EXT_CHAIN = 1u<<22,
   a_AMV_VF_EXT_LOCAL = 1u<<23, /* `local' */
   a_AMV_VF_EXT_LINKED = 1u<<24, /* `environ' link'ed */
   a_AMV_VF_EXT_FROZEN = 1u<<25, /* Has been set by -S,.. */
   a_AMV_VF_EXT_FROZEN_UNSET = 1u<<26, /* ..and was used to unset a variable */
   a_AMV_VF_EXT__FROZEN_MASK = a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET,
   a_AMV_VF_EXT__MASK = (1u<<(26+1)) - 1
};

struct a_amv_var_map{
   u32 avm_hash;
   u16 avm_keyoff;
   BITENUM_IS(u16,a_amv_var_flags) avm_flags;
};

struct a_amv_var_chain_map_bsrch{
   char avcmb_prefix[4];
   u16 avcmb_chain_map_off;
   u16 avcmb_chain_map_eokey; /* This is an enum okeys */
};

struct a_amv_var_chain_map{
   u16 avcm_keyoff;
   u16 avcm_okey;
};

#define CTA(A,S)
_EOT

   print F '#include "', $OUT, "\"\n\n";

   print F <<'_EOT';
static u8 seen_wraparound;
static size_t longest_distance;

static size_t
next_prime(size_t no){ /* blush (brute force) */
jredo:
   if((++no & 1) == 0)
      goto jredo;
   for(size_t i = 3; i < no; i += 2)
      if(no % i == 0)
         goto jredo;
   return no;
}

static size_t *
reversy(size_t size){
   struct a_amv_var_map const *vmp = a_amv_var_map,
      *vmaxp = vmp + NELEM(a_amv_var_map);
   size_t ldist = 0, *arr;

   arr = malloc(sizeof *arr * size);
   for(size_t i = 0; i < size; ++i)
      arr[i] = NELEM(a_amv_var_map);

   seen_wraparound = 0;
   longest_distance = 0;

   while(vmp < vmaxp){
      u32 hash = vmp->avm_hash, i = hash % size, l;

      for(l = 0; arr[i] != NELEM(a_amv_var_map); ++l)
         if(++i == size){
            seen_wraparound = 1;
            i = 0;
         }
      if(l > longest_distance)
         longest_distance = l;
      arr[i] = (size_t)(vmp++ - a_amv_var_map);
   }
   return arr;
}

int
main(int argc, char **argv){
   size_t *arr, size = NELEM(a_amv_var_map);

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
      "#define a_AMV_VAR_REV_ILL %zuu\n"
      "#define a_AMV_VAR_REV_PRIME %zuu\n"
      "#define a_AMV_VAR_REV_LONGEST %zuu\n"
      "#define a_AMV_VAR_REV_WRAPAROUND %d\n"
      "static %s const a_amv_var_revmap[a_AMV_VAR_REV_PRIME] = {\n%s",
      NELEM(a_amv_var_map), size, longest_distance, seen_wraparound,
      argv[1], (argc > 2 ? "  " : ""));
   for(size_t i = 0; i < size; ++i)
      printf("%s%zuu", (i == 0 ? ""
         : (i % 10 == 0 ? (argc > 2 ? ",\n  " : ",\n")
            : (argc > 2 ? ", " : ","))),
         arr[i]);
   printf("\n};\n");
   return 0;
}
_EOT
# <<<<<<<<<<<<<<<<<<<
   close F
}

sub hash_em{
   die "hash_em: open: $^E"
      unless my $pid = open2 *RFD, *WFD, $MAILX;
   foreach my $e (@ENTS){
      print WFD "csop hash32 $e->{name}\n";
      my $h = <RFD>;
      chomp $h;
      $e->{hash} = $h
   }
   print WFD "x\n";
   waitpid $pid, 0;
}

sub dump_map{
   die "$OUT: open: $^E" unless open F, '>', $OUT;
   print F '/*@ ', scalar basen($OUT), ', generated by ', scalar basen($0),
      ".\n *@ See accmacvar.c for more */\n\n";

   # Dump the names sequentially (in input order), create our map entry along
   print F 'static char const a_amv_var_names[] = {', "\n";
   my ($i, $alen, $obsolete_any) = (0, 0, 0);
   my (%virts, %defvals, %i3vals, %chains);
   foreach my $e (@ENTS){
      $e->{keyoff} = $alen;
      my $k = $e->{name};
      my $l = length $k;
      my $a = join '\',\'', split(//, $k);
      my (@fa);
      if($e->{bool}) {push @fa, 'a_AMV_VF_BOOL'}
      if($e->{virt}){
         # Virtuals are implicitly rdonly and nodel
         $e->{rdonly} = $e->{nodel} = 1;
         $virts{$k} = $e;
         push @fa, 'a_AMV_VF_VIRT'
      }
      if($e->{vip}) {push @fa, 'a_AMV_VF_VIP'}
      if($e->{rdonly}) {push @fa, 'a_AMV_VF_RDONLY'}
      if($e->{nodel}) {push @fa, 'a_AMV_VF_NODEL'}
      if(defined $e->{i3val}){
         $i3vals{$k} = $e;
         push @fa, 'a_AMV_VF_I3VAL'
      }
      if($e->{defval}){
         die "Boolean with default value: $k" if $e->{bool};
         $defvals{$k} = $e;
         push @fa, 'a_AMV_VF_DEFVAL'
      }
      if($e->{import}){
         $e->{env} = 1;
         push @fa, 'a_AMV_VF_IMPORT'
      }
      if($e->{env}) {push @fa, 'a_AMV_VF_ENV'}
      if($e->{nolopts}) {push @fa, 'a_AMV_VF_NOLOPTS'}
      if($e->{notempty}) {push @fa, 'a_AMV_VF_NOTEMPTY'}
      if($e->{nocntrls}) {push @fa, 'a_AMV_VF_NOCNTRLS'}
      if($e->{num}) {push @fa, 'a_AMV_VF_NUM'}
      if($e->{posnum}) {push @fa, 'a_AMV_VF_POSNUM'}
      if($e->{lower}) {push @fa, 'a_AMV_VF_LOWER'}
      if($e->{chain}){
         $chains{$k} = $e;
         push @fa, 'a_AMV_VF_CHAIN'
      }
      if($e->{obsolete}){
         $obsolete_any = 1;
         push @fa, 'a_AMV_VF_OBSOLETE'
      }
      $e->{flags} = \@fa;
      my $f = join('|', @fa);
      $f = ', ' . $f if length $f;
      print F "${S}/* $i. [$alen]+$l $k$f */\n" if $VERB;
      print F "${S}'$a','\\0',\n";
      ++$i;
      $alen += $l + 1
   }
   print F '};', "\n#define a_AMV_VAR_NAME_KEY_MAXOFF ${alen}u\n";
   print F "#define a_AMV_VAR_HAS_OBSOLETE\n" if $obsolete_any != 0;
   print F "\n";

   # Create the management map
   print F 'CTA(a_AMV_VF_NONE == 0, "Value not 0 as expected");', "\n";
   print F 'static struct a_amv_var_map const a_amv_var_map[] = {', "\n";
   foreach my $e (@ENTS){
      my $f = $VERB ? 'a_AMV_VF_NONE' : '0';
      my $fa = join '|', @{$e->{flags}};
      $f .= '|' . $fa if length $fa;
      print F "${S}{$e->{hash}u, $e->{keyoff}u, $f},";
      if($VERB) {print F "${S}/* $e->{name} */\n"}
      else {print F "\n"}
   }
   print F '};', "\n\n";

   # The rest not to be injected for this generator script
   print F <<_EOT;
#ifndef a__CREATE_OKEY_MAP_PL
# undef a_X
# ifdef mx_HAVE_PUTENV
#  define a_X(X) X
# else
#  define a_X(X)
# endif

_EOT

   #
   if(%chains){
      my (@prefixes,$last_pstr,$last_pbeg,$last_pend,$i);
      print F 'CTAV(4 == ',
         'FIELD_SIZEOF(struct a_amv_var_chain_map_bsrch, avcmb_prefix));',
         "\n";
      print F 'static struct a_amv_var_chain_map const ',
         'a_amv_var_chain_map[] = {', "\n";
      $last_pstr = "";
      $last_pend = "n_OKEYS_MAX";
      $last_pbeg = $i = 0;
      foreach my $e (sort {CORE::fc($a) cmp CORE::fc($b)} keys %chains){
         $e = $chains{$e};
         print F "${S}{$e->{keyoff}u, $e->{enum}},\n";
         die "Chains need length of at least 4 bytes: $e->{name}"
            if length $e->{name} < 4;
         my $p = substr $e->{name}, 0, 4;
         if($p ne $last_pstr){
            push @prefixes, [$last_pstr, $last_pbeg, $last_pend] if $i > 0;
            $last_pstr = $p;
            $last_pbeg = $i
         }
         $last_pend = $e->{enum};
         ++$i
      }
      push @prefixes, [$last_pstr, $last_pbeg, $last_pend] if $last_pstr ne "";
      print F '};', "\n";
      print F '#define a_AMV_VAR_CHAIN_MAP_CNT ', scalar %chains, "\n\n";

      print F 'static struct a_amv_var_chain_map_bsrch const ',
         'a_amv_var_chain_map_bsrch[] = {', "\n";
      foreach my $e (@prefixes){
         print F "${S}{\"$e->[0]\", $e->[1]u, $e->[2]},\n"
      }
      print F '};', "\n";
      print F '#define a_AMV_VAR_CHAIN_MAP_BSRCH_CNT ',
         scalar @prefixes, "\n\n"
   }

   # Virtuals are _at least_ the versioning variables
   # The problem is that struct var uses a variable sized character buffer
   # which cannot be initialized in a conforming way :(
   print F '/* Unfortunately init of varsized buffer impossible: ' .
      'define "subclass"es */' . "\n";
   my @skeys = sort {CORE::fc($a) cmp CORE::fc($b)} keys %virts;
   foreach(@skeys){
      my $e = $virts{$_};
      $e->{vname} = $1 if $e->{enum} =~ /ok_._(.*)/;
      $e->{vstruct} = "var_virt_$e->{vname}";
      print F "static char const a_amv_$e->{vstruct}_val[] = {$e->{virt}};\n";
      print F "static struct{\n";
      print F "${S}struct a_amv_var *av_link;\n";
      print F "${S}char const *av_value;\n";
      print F "${S}a_X(char *av_env;)\n";
      print F "${S}u32 av_flags;\n";
      print F "${S}char const av_name[", length($e->{name}), " +1];\n";
      my $f = $VERB ? 'a_AMV_VF_NONE' : '0';
      my $fa = join '|', @{$e->{flags}};
      $f .= '|' . $fa if length $fa;
      print F "} const a_amv_$e->{vstruct} = ",
         "{NIL, a_amv_$e->{vstruct}_val, a_X(0 su_COMMA) $f, ",
         "\"$e->{name}\"};\n\n"
   }

   print F "\n" unless @skeys;
   print F 'static struct a_amv_var_virt const a_amv_var_virts[] = {', "\n";
   foreach(@skeys){
      my $e = $virts{$_};
      my $n = $1 if $e->{enum} =~ /ok_._(.*)/;
      print F "${S}{$e->{enum}, {0,}, (void const*)&a_amv_$e->{vstruct}},\n";
   }
   print F "};\n";
   print F '#define a_AMV_VAR_VIRTS_CNT ', scalar @skeys, "\n";

   # First-time-init values
   @skeys = sort {CORE::fc($a) cmp CORE::fc($b)} keys %i3vals;
   print F "\n";
   print F 'static struct a_amv_var_defval const a_amv_var_i3vals[] = {', "\n";
   foreach(@skeys){
      my $e = $i3vals{$_};
      print F "${S}{", $e->{enum}, ', {0,}, ',
         (!$e->{bool} ? $e->{i3val} : "NIL"), "},\n"
   }
   print F "};\n";
   print F '#define a_AMV_VAR_I3VALS_CNT ', scalar @skeys, "\n";

   # Default values
   @skeys = sort {CORE::fc($a) cmp CORE::fc($b)} keys %defvals;
   print F "\n";
   print F 'static struct a_amv_var_defval const a_amv_var_defvals[] = {',
      "\n";
   foreach(@skeys){
      my $e = $defvals{$_};
      print F "${S}{", $e->{enum}, ', {0,}, ',
         (!$e->{bool} ? $e->{defval} : "NIL"), "},\n"
   }
   print F "};\n";
   print F '#define a_AMV_VAR_DEFVALS_CNT ', scalar @skeys, "\n";

   # Special var backing [#@*?]|[1-9][0-9]*|0
   $i = 0;
   print F "\n";
   foreach my $e (@ENTS){
      if($e->{name} eq '--special-param'){
         print F "#define a_AMV_VAR__SPECIAL_PARAM_MAP_IDX ${i}u\n"
      }
      # The rest are only speedups
      elsif($e->{name} eq '?'){
         print F "#define a_AMV_VAR__QM_MAP_IDX ${i}u\n"
      }elsif($e->{name} eq '!'){
         print F "#define a_AMV_VAR__EM_MAP_IDX ${i}u\n"
      }
      ++$i
   }

   print F "\n";
   print F "# undef a_X\n";
   print F "#endif /* !a__CREATE_OKEY_MAP_PL */\n";
   die "$OUT: close: $^E" unless close F
}

sub reverser{
   my $argv2 = $VERB ? ' verb' : '';
   system("\$CC -I. -o $CTOOL_EXE $CTOOL");
   my $t = (@ENTS < 0xFF ? 'u8' : (@ENTS < 0xFFFF ? 'u16' : 'u32'));
   `$CTOOL_EXE $t$argv2 >> $OUT`
}

{package main; main_fun()}

# s-it-mode
