#!/usr/bin/env perl
require 5.008_001;
use utf8;
#@ Parse 'enum okeys' from nail.h and create gen-okeys.h.  And see accmacvar.c.
# Public Domain

# Acceptable "longest distance" from hash-modulo-index to key
my $MAXDISTANCE_PENALTY = 5;

# Generate a more verbose output.  Not for shipout versions.
my $VERB = 1;

my $OUT = 'gen-okeys.h';

##  --  >8  --  8<  --  ##

use diagnostics -verbose;
use strict;
use warnings;

use sigtrap qw(handler cleanup normal-signals);

my ($S, @ENTS, $CTOOL, $CTOOL_EXE) = ($VERB ? '   ' : '');

sub main_fun{
   if(@ARGV) {$VERB = 0; $S = ''}

   parse_nail_h();

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

sub parse_nail_h{
   die "nail.h: open: $^E" unless open F, '<', 'nail.h';
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
         while($x && $x =~ /^([^,]+?)(?:,(.*))?$/){
            $x = $2;
            $1 =~ /([^=]+)=(.+)/;
            die "Unsupported special directive: $1"
               if($1 ne 'name' &&
                  $1 ne 'virt' && $1 ne 'nolopts' &&
                  $1 ne 'rdonly' && $1 ne 'nodel' && $1 ne 'notempty' &&
                     $1 ne 'nocntrls' &&
                  $1 ne 'num' && $1 ne 'posnum' && $1 ne 'lower' &&
                  $1 ne 'vip' && $1 ne 'import' && $1 ne 'env' &&
                  $1 ne 'i3val' && $1 ne 'defval');
            $vals{$1} = $2
         }
      }
      push @ENTS, \%vals
   }
   if($init != 2) {die 'nail.h does not have the expected content'}
   close F
}

sub create_c_tool{
   $CTOOL = './tmp-okey-tool-' . $$ . '.c';
   $CTOOL_EXE = $CTOOL . '.exe';

   die "$CTOOL: open: $^E" unless open F, '>', $CTOOL;
   # xxx optimize: could read lines and write lines in HASH_MODE..
   print F '#define MAX_DISTANCE_PENALTY ', $MAXDISTANCE_PENALTY, "\n";
# >>>>>>>>>>>>>>>>>>>
   print F <<'_EOT';
#define __CREATE_OKEY_MAP_PL
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define n_NELEM(A) (sizeof(A) / sizeof(A[0]))

#define ui32_t uint32_t
#define ui16_t uint16_t
#define ui8_t uint8_t

enum a_amv_var_flags{
   a_AMV_VF_NONE = 0,
   a_AMV_VF_BOOL = 1<<0,      /* ok_b_* */
   a_AMV_VF_VIRT = 1<<1,      /* "Stateless" automatic variable */
   a_AMV_VF_NOLOPTS = 1<<2,   /* May not be tracked by `localopts' */
   a_AMV_VF_RDONLY = 1<<3,    /* May not be set by user */
   a_AMV_VF_NODEL = 1<<4,     /* May not be deleted */
   a_AMV_VF_NOTEMPTY = 1<<5,  /* May not be assigned an empty value */
   a_AMV_VF_NOCNTRLS = 1<<6,  /* Value may not contain control characters */
   a_AMV_VF_NUM = 1<<7,       /* Value must be a 32-bit number */
   a_AMV_VF_POSNUM = 1<<8,    /* Value must be positive 32-bit number */
   a_AMV_VF_LOWER = 1<<9,     /* Values will be stored in a lowercase version */
   a_AMV_VF_VIP = 1<<10,      /* Wants _var_check_vips() evaluation */
   a_AMV_VF_IMPORT = 1<<11,   /* Import ONLY from environ (pre n_PSO_STARTED) */
   a_AMV_VF_ENV = 1<<12,      /* Update environment on change */
   a_AMV_VF_I3VAL = 1<<13,    /* Has an initial value */
   a_AMV_VF_DEFVAL = 1<<14,   /* Has a default value */
   a_AMV_VF_LINKED = 1<<15,   /* `environ' linked */
   a_AMV_VF__MASK = (1<<(15+1)) - 1
};

struct a_amv_var_map{
   ui32_t avm_hash;
   ui16_t avm_keyoff;
   ui16_t avm_flags;    /* enum a_amv_var_flags */
};

#ifdef HASH_MODE
/* NOTE: copied over verbatim from auxlily.c */
static ui32_t
torek_hash(char const *name){
   /* Chris Torek's hash.
    * NOTE: need to change *at least* mk-okey-map.pl when changing the
    * algorithm!! */
   ui32_t h = 0;

   while(*name != '\0'){
      h *= 33;
      h += *name++;
   }
   return h;
}

#else
  /* Include what has been written in HASH_MODE */
# define n_CTA(A,S)
# include "gen-okeys.h"

static ui8_t seen_wraparound;
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
   struct a_amv_var_map const *vmp = a_amv_var_map,
      *vmaxp = vmp + n_NELEM(a_amv_var_map);
   size_t ldist = 0, *arr;

   arr = malloc(sizeof *arr * size);
   for(size_t i = 0; i < size; ++i)
      arr[i] = n_NELEM(a_amv_var_map);

   seen_wraparound = 0;
   longest_distance = 0;

   while(vmp < vmaxp){
      ui32_t hash = vmp->avm_hash, i = hash % size, l;

      for(l = 0; arr[i] != n_NELEM(a_amv_var_map); ++l)
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
#endif /* !HASH_MODE */

int
main(int argc, char **argv){
#ifdef HASH_MODE
   size_t h = torek_hash(argv[1]);

   printf("%lu\n", (unsigned long)h);

#else
   size_t *arr, size = n_NELEM(a_amv_var_map);

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
      n_NELEM(a_amv_var_map), size, longest_distance, seen_wraparound,
      argv[1], (argc > 2 ? "  " : ""));
   for(size_t i = 0; i < size; ++i)
      printf("%s%zuu", (i == 0 ? ""
         : (i % 10 == 0 ? (argc > 2 ? ",\n  " : ",\n")
            : (argc > 2 ? ", " : ","))),
         arr[i]);
   printf("\n};\n");
#endif
   return 0;
}
_EOT
# <<<<<<<<<<<<<<<<<<<
   close F
}

sub hash_em{
   system("c99 -DHASH_MODE -I. -o $CTOOL_EXE $CTOOL");

   foreach my $e (@ENTS){
      my $h = `$CTOOL_EXE $e->{name}`;
      chomp $h;
      $e->{hash} = $h
   }
}

sub dump_map{
   die "$OUT: open: $^E" unless open F, '>', $OUT;
   print F "/*@ $OUT, generated by $0 on ", scalar gmtime(), ".\n",
      " *@ See accmacvar.c for more */\n\n";

   print F 'static char const a_amv_var_names[] = {', "\n";
   my ($i, $alen) = (0, 0);
   my (%virts, %defvals, %i3vals);
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
      if(defined $e->{i3val}){
         $i3vals{$k} = $e;
         push @fa, 'a_AMV_VF_I3VAL'
      }
      if($e->{defval}){
         $e->{notempty} = 1;
         $defvals{$k} = $e;
         push @fa, 'a_AMV_VF_DEFVAL'
      }
      if($e->{import}){
         $e->{env} = 1;
         push @fa, 'a_AMV_VF_IMPORT'
      }
      if($e->{nolopts}) {push @fa, 'a_AMV_VF_NOLOPTS'}
      if($e->{rdonly}) {push @fa, 'a_AMV_VF_RDONLY'}
      if($e->{nodel}) {push @fa, 'a_AMV_VF_NODEL'}
      if($e->{notempty}) {push @fa, 'a_AMV_VF_NOTEMPTY'}
      if($e->{nocntrls}) {push @fa, 'a_AMV_VF_NOCNTRLS'}
      if($e->{num}) {push @fa, 'a_AMV_VF_NUM'}
      if($e->{posnum}) {push @fa, 'a_AMV_VF_POSNUM'}
      if($e->{lower}) {push @fa, 'a_AMV_VF_LOWER'}
      if($e->{vip}) {push @fa, 'a_AMV_VF_VIP'}
      if($e->{env}) {push @fa, 'a_AMV_VF_ENV'}
      $e->{flags} = \@fa;
      my $f = join('|', @fa);
      $f = ', ' . $f if length $f;
      print F "${S}/* $i. [$alen]+$l $k$f */\n" if $VERB;
      print F "${S}'$a','\\0',\n";
      ++$i;
      $alen += $l + 1
   }
   print F '};', "\n\n";

   print F 'n_CTA(a_AMV_VF_NONE == 0, "Value not 0 as expected");', "\n";
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

   # We have at least version stuff in here
   # The problem is that struct var uses a variable sized character buffer
   # which cannot be initialized in a conforming way :(
   print F <<_EOT;
#ifndef __CREATE_OKEY_MAP_PL
# ifdef HAVE_PUTENV
#  define a_X(X) X
# else
#  define a_X(X)
# endif

/* Unfortunately init of varsized buffer won't work: define "subclass"es */
_EOT
   my @skeys = sort keys %virts;

   foreach(@skeys){
      my $e = $virts{$_};
      $e->{vname} = $1 if $e->{enum} =~ /ok_._(.*)/;
      $e->{vstruct} = "var_virt_$e->{vname}";
      print F "static char const a_amv_$e->{vstruct}_val[] = {$e->{virt}};\n";
      print F "static struct{\n";
      print F "${S}struct a_amv_var *av_link;\n";
      print F "${S}char const *av_value;\n";
      print F "${S}a_X(char *av_env;)\n";
      print F "${S}ui16_t av_flags;\n";
      print F "${S}char const av_name[", length($e->{name}), " +1];\n";
      my $f = $VERB ? 'a_AMV_VF_NONE' : '0';
      my $fa = join '|', @{$e->{flags}};
      $f .= '|' . $fa if length $fa;
      print F "} const a_amv_$e->{vstruct} = ",
         "{NULL, a_amv_$e->{vstruct}_val, a_X(0 COMMA) $f, ",
         "\"$e->{name}\"};\n\n"
   }
   print F "# undef a_X\n";

   print F "\n";
   print F '#define a_AMV_VAR_VIRTS_CNT ', scalar @skeys, "\n";
   print F 'static struct a_amv_var_virt const a_amv_var_virts[] = {', "\n";
   foreach(@skeys){
      my $e = $virts{$_};
      my $n = $1 if $e->{enum} =~ /ok_._(.*)/;
      print F "${S}{$e->{enum}, {0,}, (void const*)&a_amv_$e->{vstruct}},\n";
   }
   print F "};\n";

   #
   @skeys = sort keys %i3vals;

   print F "\n";
   print F '#define a_AMV_VAR_I3VALS_CNT ', scalar @skeys, "\n";
   print F 'static struct a_amv_var_defval const a_amv_var_i3vals[] = {', "\n";
   foreach(@skeys){
      my $e = $i3vals{$_};
      print F "${S}{", $e->{enum}, ', {0,}, ',
         (!$e->{bool} ? $e->{i3val} : "NULL"), "},\n"
   }
   print F "};\n";

   #
   @skeys = sort keys %defvals;

   print F "\n";
   print F '#define a_AMV_VAR_DEFVALS_CNT ', scalar @skeys, "\n";
   print F 'static struct a_amv_var_defval const a_amv_var_defvals[] = {', "\n";
   foreach(@skeys){
      my $e = $defvals{$_};
      print F "${S}{", $e->{enum}, ', {0,}, ',
         (!$e->{bool} ? $e->{defval} : "NULL"), "},\n"
   }
   print F "};\n";

   print F "#endif /* __CREATE_OKEY_MAP_PL */\n";

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
   die "$OUT: close: $^E" unless close F
}

sub reverser{
   my $argv2 = $VERB ? ' verb' : '';
   system("c99 -I. -o $CTOOL_EXE $CTOOL");
   my $t = (@ENTS < 0xFF ? 'ui8_t' : (@ENTS < 0xFFFF ? 'ui16_t' : 'ui32_t'));
   `$CTOOL_EXE $t$argv2 >> $OUT`
}

{package main; main_fun()}

# s-it-mode
