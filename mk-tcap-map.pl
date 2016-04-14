#!/usr/bin/env perl
require 5.008_001;
use utf8;
#@ Parse 'enum n_termcap_{cmd,query}' from nail.h and create tcaps.h.
#@ And see termcap.c.
# Public Domain

my $OUT = 'tcaps.h';

# Generate a more verbose output.  Not for shipout versions.
my $VERB = 1;

##  --  >8  --  8<  --  ##

use diagnostics -verbose;
use strict;
use warnings;

my ($S, $CAPS_LEN, $BIND_START, @CAPS_NAMES, @ENTS) =
      (($VERB ? '   ' : ''), 0, -1);

sub main_fun{
   if(@ARGV) {$VERB = 0; $S = ''}

   parse_nail_h();

   dump_data();
   exit 0
}

sub parse_nail_h{
   die "nail.h: open: $^E" unless open F, '<', 'nail.h';
   my ($init) = (0);
   while(<F>){
      chomp;

      # Only want the enum okeys content
      if(/^enum n_termcap_cmd/){
         $init = 1;
         next
      }
      if(!$init && /^enum n_termcap_query/){
         $init = 2;
         next
      }
      if(/^\};/){
         if($init == 2){
            $init = 3;
            last
         }
         $init = 0;
         next
      }
      $init || next;

      # Ignore empty and comment lines
      /^$/ && next;
      if(/^\s*\/\*/){
         # However, one special directive we know
         $BIND_START = @CAPS_NAMES + 1 if /--mk-tcap-map--/;
         next
      }

      # We need to preserve preprocessor conditionals
      if(/^\s*#/){
         push @ENTS, [$_];
         next
      }

      # An entry is a constant followed by a specially crafted comment;
      # ignore anything else
      /^\s*(\w+),\s*
         \/\*\s*(\w+|-)\s*\/\s*([^,\s]+|-),
         \s*(\w+)\s*
         (?:,\s*(\w+)\s*)?
         (?:\||\*\/)
      /x;
      next unless $1 && $2 && $3 && $4;
      die "Unsupported terminal capability type: $4"
         unless($4 eq 'BOOL' || $4 eq 'NUMERIC' || $4 eq 'STRING');

      my $e = 'n_TERMCAP_CAPTYPE_' . $4;
      $e = $e . '|a_TERMCAP_F_QUERY' if $init == 2;
      $e = $e . '|a_TERMCAP_F_ARG_' . $5 if $5;
      push @ENTS, [$1, $e, $CAPS_LEN];
      # termcap(5) names are always two chars, place them first, don't add NUL
      my ($ti, $tc) = ($2, $3);
      $tc = '' if $tc eq '-';
      $ti = '' if $ti eq '-';
      my $l = 2 +0 + length($ti) +1;
      push @CAPS_NAMES, [$1, $CAPS_LEN, $l, $tc, $ti];
      $CAPS_LEN += $l;
   }
   die 'nail.h does not have the expected content' unless $init == 3;
   close F
}

sub dump_data{
   die "$OUT: open: $^E" unless open F, '>', $OUT;
   print F "/*@ $OUT, generated by $0 on ", scalar gmtime(), ".\n",
      " *@ See termcap.c for more */\n\n";

   print F 'static char const a_termcap_namedat[] = {', "\n";
   foreach my $np (@CAPS_NAMES){
      sub _exp{
         if(length $_[0]){
            $_[0] = '\'' . join('\',\'', split(//, $_[0])) . '\',';
         }elsif($_[1] > 0){
            $_[0] = '\'\0\',' x $_[1];
         }
      }

      if($BIND_START > 0){
         print F '#ifdef HAVE_KEY_BINDINGS', "\n" if(--$BIND_START == 0)
      }
      my ($tcn, $tin) = (_exp(scalar $np->[3], 2), _exp(scalar $np->[4], 0));
      if($VERB){
         print F "${S}/* [$np->[1]]+$np->[2], $np->[0] */ $tcn  $tin'\\0',\n"
      }else{
         print F "${S}$tcn $tin'\\0',\n"
      }
   }
   print F '#endif /* HAVE_KEY_BINDINGS */', "\n" if($BIND_START == 0);
   print F '};', "\n\n";

   print F 'static struct a_termcap_control const a_termcap_control[] = {',
      "\n";
   my $i = 0;
   foreach my $ent (@ENTS){
      if($#$ent == 0){
         print F $ent->[0], "\n"
      }else{
         if($VERB){
            print F ${S}, '{/* ', $i, '. ', $ent->[0], ' */ ', $ent->[1], ', ',
               $ent->[2], "},\n"
         }else{
            print F "{$ent->[1], $ent->[2]},\n"
         }
         ++$i
      }
   }
   print F '};', "\n";

   die "$OUT: close: $^E" unless close F
}

{package main; main_fun()}

# s-it-mode
