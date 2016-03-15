#!/usr/bin/env perl
require 5.008_001;
use utf8;
#@ Parse 'enum okeys' from nail.h and create okeys.h.  And see accmacvar.c.
# Public Domain

# Acceptable "longest distance" from hash-modulo-index to key
my $MAXDISTANCE_PENALTY = 5;

my $OUT = 'okeys.h';

##  --  >8  --  8<  --  ##

use diagnostics -verbose;
use strict;
use warnings;

use sigtrap qw(handler cleanup normal-signals);

my (@ENTS, $CTOOL, $CTOOL_EXE);

sub main_fun {
   parse_nail_h();

   create_c_tool();

   hash_em();
   dump_keydat_varmap();

   reverser();

   cleanup(undef);
   exit 0
}

sub cleanup {
   die "$CTOOL_EXE: couldn't unlink: $^E"
      if $CTOOL_EXE && -f $CTOOL_EXE && 1 != unlink $CTOOL_EXE;
   die "$CTOOL: couldn't unlink: $^E"
      if $CTOOL && -f $CTOOL && 1 != unlink $CTOOL;
   die "Terminating due to signal $_[0]" if $_[0]
};

sub parse_nail_h {
   die "nail.h: open: $^E" unless open F, '<', 'nail.h';
   my ($init) = (0);
   while (<F>) {
      # Only want the enum okeys content
      if (/^enum okeys/) {$init = 1; next}
      if (/^};/) {if ($init) {$init = 2; last}; next}
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
      $vals{boolean} = ($k =~ /^ok_b/ ? 1 : 0);
      $k = $1 if $k =~ /^ok_[bv]_(.+)$/;
      $k =~ s/_/-/g;
      $vals{name} = $k;
      if ($x) {
         while ($x && $x =~ /^([^,]+?)(?:,(.*))?$/) {
            $x = $2;
            $1 =~ /([^=]+)=(.+)/;
            die "Unsupported special directive: $1"
               if ($1 ne 'name' && $1 ne 'rdonly' &&
                   $1 ne 'special' && $1 ne 'virtual');
            $vals{$1} = $2
         }
      }
      push @ENTS, \%vals
   }
   if ($init != 2) {die 'nail.h does not have the expected content'}
   close F
}

sub create_c_tool {
   $CTOOL = './tmp-okey-tool-' . $$ . '.c';
   $CTOOL_EXE = $CTOOL . '.exe';

   die "$CTOOL: open: $^E" unless open F, '>', $CTOOL;
   # xxx optimize: could read lines and write lines in HASH_MODE..
   print F '#define MAX_DISTANCE_PENALTY ', $MAXDISTANCE_PENALTY, "\n";
# >>>>>>>>>>>>>>>>>>>
   print F <<'__EOT';
#define __CREATE_OKEY_MAP_PL
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef NELEM
# define NELEM(A) (sizeof(A) / sizeof(A[0]))
#endif

#define ui32_t    uint32_t
#define ui16_t    uint16_t
#define ui8_t     uint8_t

enum var_map_flags {
   VM_NONE     = 0,
   VM_BOOLEAN  = 1<<0,           /* ok_b_* */
   VM_RDONLY   = 1<<1,           /* May not be set by user */
   VM_SPECIAL  = 1<<2,           /* Wants _var_check_specials() evaluation */
   VM_VIRTUAL  = 1<<3            /* "Stateless": no var* -- implies VM_RDONLY */
};

/* Binary compatible with struct var! (xxx make it superclass?) */
struct var_vx {
   struct var     *v_link;
   char           *v_value;
};

struct var_virtual {
   ui32_t         vv_okey;
   struct var_vx  vv_var;
};

struct var_map {
   ui32_t         vm_hash;
   ui16_t         vm_keyoff;
   ui16_t         vm_flags;      /* var_map_flags bits */
};

#ifdef HASH_MODE
/* NOTE: copied over verbatim from auxlily.c */
static ui32_t
torek_hash(char const *name)
{
   /* Chris Torek's hash.
    * NOTE: need to change *at least* create-okey-map.pl when changing the
    * algorithm!! */
	ui32_t h = 0;

	while (*name != '\0') {
		h *= 33;
		h += *name++;
	}
	return h;
}

#else
  /* Include what has been written in HASH_MODE */
# include "okeys.h"

static ui8_t   seen_wraparound;
static size_t  longest_distance;

static size_t
next_prime(size_t no) /* blush (brute force) */
{
jredo:
   ++no;
   for (size_t i = 3; i < no; i += 2)
      if (no % i == 0)
         goto jredo;
   return no;
}

static size_t *
reversy(size_t size)
{
   struct var_map const *vmp = _var_map, *vmaxp = vmp + NELEM(_var_map);
   size_t ldist = 0, *arr;

   arr = malloc(sizeof *arr * size);
   for (size_t i = 0; i < size; ++i)
      arr[i] = NELEM(_var_map);

   seen_wraparound = 0;
   longest_distance = 0;

   while (vmp < vmaxp) {
      ui32_t hash = vmp->vm_hash, i = hash % size, l;

      for (l = 0; arr[i] != NELEM(_var_map); ++l)
         if (++i == size) {
            seen_wraparound = 1;
            i = 0;
         }
      if (l > longest_distance)
         longest_distance = l;
      arr[i] = (size_t)(vmp++ - _var_map);
   }
   return arr;
}
#endif /* !HASH_MODE */

int
main(int argc, char **argv)
{
#ifdef HASH_MODE
   size_t h = torek_hash(argv[1]);

   printf("%lu\n", (unsigned long)h);

#else
   size_t *arr, size = NELEM(_var_map);

   fprintf(stderr, "Starting reversy, okeys=%zu\n", size);
   for (;;) {
      arr = reversy(size = next_prime(size));
      fprintf(stderr, " - size=%zu longest_distance=%zu seen_wraparound=%d\n",
         size, longest_distance, seen_wraparound);
      if (longest_distance <= MAX_DISTANCE_PENALTY)
         break;
      free(arr);
   }

   printf(
      "#define _VAR_REV_ILL         %zuu\n"
      "#define _VAR_REV_PRIME       %zuu\n"
      "#define _VAR_REV_LONGEST     %zuu\n"
      "#define _VAR_REV_WRAPAROUND  %d\n"
      "static %s const _var_revmap[_VAR_REV_PRIME] = {\n   ",
      NELEM(_var_map), size, longest_distance, seen_wraparound, argv[1]);
   for (size_t i = 0; i < size; ++i)
      printf("%s%zuu", (i == 0 ? "" : (i % 10 == 0 ? ",\n   " : ", ")), arr[i]);
   printf("\n};\n");
#endif
   return 0;
}
__EOT
# <<<<<<<<<<<<<<<<<<<
   close F
}

sub hash_em {
   system("c99 -DHASH_MODE -I. -o $CTOOL_EXE $CTOOL");

   foreach my $e (@ENTS) {
      my $h = `$CTOOL_EXE $e->{name}`;
      chomp $h;
      $e->{hash} = $h
   }
}

sub dump_keydat_varmap {
   die "$OUT: open: $^E" unless open F, '>', $OUT;
   print F "/*@ $OUT, generated by $0 on ", scalar gmtime(), ".\n",
      " *@ See accmacvar.c for more */\n\n";

   print F 'static char const _var_keydat[] = {', "\n";
   my ($i, $alen) = (0, 0);
   my %virts;
   foreach my $e (@ENTS) {
      $e->{keyoff} = $alen;
      my $k = $e->{name};
      my $l = length $k;
      my $a = join '\',\'', split(//, $k);
      my ($f, $s) = ('', ', ');
      if ($e->{boolean}) {$f .= $s . 'VM_BOOLEAN'; $s = ' | '}
      if ($e->{rdonly})  {$f .= $s . 'VM_RDONLY'; $s = ' | '}
      if ($e->{special}) {$f .= $s . 'VM_SPECIAL'; $s = ' | '}
      if ($e->{virtual}) {
         die("*$k*: virtual MUST be rdonly, too!") unless $e->{rdonly};
         $f .= $s . 'VM_VIRTUAL'; $s = ' | ';
         $virts{$k} = $e;
      }
      print F "   /* $i. [$alen]+$l $k$f */\n", "   '$a','\\0',\n";
      ++$i;
      $alen += $l + 1
   }
   print F '};', "\n\n";

   print F 'static struct var_map const _var_map[] = {', "\n";
   foreach my $e (@ENTS) {
      my $f = 'VM_NONE';
      $f .= ' | VM_BOOLEAN' if $e->{boolean};
      $f .= ' | VM_RDONLY'  if $e->{rdonly};
      $f .= ' | VM_SPECIAL' if $e->{special};
      $f .= ' | VM_VIRTUAL' if $e->{virtual};
      my $n = $1 if $e->{enum} =~ /ok_._(.*)/;
      print F "   {$e->{hash}u, $e->{keyoff}u, $f}, /* $n */\n"
   }
   print F '};', "\n\n";

   # We have at least version stuff in here
   # The problem is that struct var uses a variable sized character buffer
   # which cannot be initialized in a conforming way :(
   print F "#ifndef __CREATE_OKEY_MAP_PL\n";
   print F " /* Unfortunately init of varsized buffer won't work */\n";
   foreach my $k (keys %virts) {
      my $e = $virts{$k};
      $e->{vname} = $1 if $e->{enum} =~ /ok_._(.*)/;
      $e->{vstruct} = "var_virt_$e->{vname}";
      print F "static struct {\n";
      print F "   struct var *v_link;\n";
      print F "   char const *v_value;\n";
      print F "   char const v_name[", length($e->{name}), " +1];\n";
      print F "} const _$e->{vstruct} = ",
         "{NULL, $e->{virtual}, \"$e->{name}\"};\n\n";
   }

   print F 'static struct var_virtual const _var_virtuals[] = {', "\n";
   foreach my $k (keys %virts) {
      my $e = $virts{$k};
      my $n = $1 if $e->{enum} =~ /ok_._(.*)/;
      print F "   {$e->{enum}, (void const*)&_$e->{vstruct}},\n";
   }
   print F "};\n#endif /* __CREATE_OKEY_MAP_PL */\n\n";

   die "$OUT: close: $^E" unless close F
}

sub reverser {
   system("c99 -I. -o $CTOOL_EXE $CTOOL");
   my $t = (@ENTS < 0xFF ? 'ui8_t' : (@ENTS < 0xFFFF ? 'ui16_t' : 'ui32_t'));
   `$CTOOL_EXE $t >> $OUT`
}

{package main; main_fun()}

# s-it-mode
