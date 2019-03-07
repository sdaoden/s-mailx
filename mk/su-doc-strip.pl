#!/usr/bin/env perl
require 5.008_001;
#@ Strip *! style documents from C/C++ code.
#@ Assumes that such lines are exclusive, and that /*!< */ lines
#@ are followed by no other such comment.
#@ Synopsis: su-doc-strip.pl :FILE:
# Public Domain

##  --  >8  --  8<  --  ##

use diagnostics -verbose;
use strict;
use warnings;

sub main_fun{
   die 'False usage' if @ARGV == 0;

   while(@ARGV > 0){
      my ($f, $i, $fd, @lines) = (shift @ARGV, 0);

      # Read it, stripping the comments
      die "Cannot open $f for reading: $^E"
         unless open $fd, '<:raw', $f;
      while(<$fd>){
         chomp;
         if($i){
            if($_ =~ /^(.*?)\*\/[[:space:]]*(.*)$/){
               $i = 0;
               push @lines, $2 if length $2;
            }
         }elsif($_ =~ /^(.*?)[[:space:]]*\/\*!(.*)$/){
            my ($m1, $m2) = ($1, $2);
            if($m2 =~ /^(.*?)\*\/[[:space:]]*(.*)$/){
               my $l = $m1 . $2;
               push @lines, $l if length($l)
            }else{
               $i = 1;
               push @lines, $m1 if length $m1
            }
         }else{
            push @lines, $_ if length $_
         }
      }
      die "Cannot close $f after reading: $^E"
         unless close $fd;

      die "Cannot open $f for writing: $^E"
         unless open $fd, '>:raw', $f;
      while(@lines){
         $i = shift @lines;
         die "Cannot write to $f: $^E"
            unless print $fd $i, "\n";
      }
      die "Cannot close $f after writing: $^E"
         unless close $fd
   }
   exit 0
}

{package main; main_fun()}

# s-it-mode
