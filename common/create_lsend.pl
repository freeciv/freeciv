#!/usr/bin/perl -w
#
# This script creates the files packets_lsend.h and packets_lsend.c,
# based on packets.h.  The created files contain "lsend_packet_*"
# functions which send a packet to a list of connections, iterating
# the list and calling corresponding "send_packet_*" functions from
# packets.h.
#
# Optional argument to this script is the directory containing
# packets.h; the output files are created in the same directory (old
# output files are overwritten if they exist).  The default is the
# current directory.
#
# Note the created header file is included in packets.h, and should
# not be included anywhere else.  (Thus it does not need the usual
# #ifdef FC__FILENAME_H protection against multiple inclusion, since
# the contents of packets.h are already protected.)
#
# The lsend functions are only used in the server, but IMO it seems
# easier and more natural to define them here in common.
#
# This script was written by David W. Pfitzner <dwp@mso.anu.edu.au>, 
# July 2000.  This script is hereby placed in the Public Domain.

use strict;

my $prog = $0;
$prog =~ s(.*/)();

my $usage = "$prog: usage: $prog [dir]";

# Arg:
my $dir;
if (@ARGV==0) {
    $dir = ".";
} elsif(@ARGV==1) {
    $dir = $ARGV[0];
} else {
    die "$usage\n";
}

# Files:
my $input = "$dir/packets.h";
my $out_h = "$dir/packets_lsend.h";
my $out_c = "$dir/packets_lsend.c";

# Open files:
open(INPUT, "< $input ") or die "Could not open input $input: $!";
open(OUT_H, "> $out_h ") or die "Could not open output $out_h: $!";
open(OUT_C, "> $out_c ") or die "Could not open output $out_c: $!";

# Slurp the whole input file:
my $data;
{
    local $/;
    $/ = undef;
    $data = <INPUT>;
}
close(INPUT);

# Get first comment block from input (copyright statement) and write 
# to both output files:
if ($data =~ m!(/\*.*?\*/)!s) {	  # non-greedy multiline comment
    my $comment = $1 . "\n";
    print OUT_H $comment;
    print OUT_C $comment;
}

# Write comments about auto-generation etc:
{
    my $stars = "***********************************" 
	. "***********************************";
    my $gen = "  This file was auto-generated, by $prog (must be run manually)";
    my $inc = "  This file should only be included via packets.h";
    print OUT_H "\n/$stars\n$gen\n$inc\n$stars/\n\n";
    print OUT_C "\n/$stars\n$gen\n$stars/\n\n";
}

# packets.h should include packet_l.h
print OUT_C "\#include \"packets.h\"\n\n";

# Find each send_packet_* prototype:
while ($data =~ m/int \s+ send_packet_(\w+)   # function name
                  \s* \( 
                  ( [^\)]* )                  # args, no right bracket inside
                  \) \s* ; /sxg) {
    my $packet = $1;
    my $args = $2;

    my $new_args = $args;
    if ($new_args !~ s/^\s* struct \s* connection \s* \* \s* \w+
	              /struct conn_list *dest/sx) {
	warn "Funny first arg for $packet: $args\n";
	next;
    }
    
    # Get argument names:
    my @args = split(/\s*,\s*/s, $args);
    shift @args;                   # ignore first arg - is connection
    foreach my $arg (@args) {
	$arg =~ s/(.*)\b(\w+)$/$2/;   # only keep final "word"; changes @args
    }
    my $call_args = join(", ", @args);
    if (@args) {
	$call_args = ", " . $call_args;
    }
    ##print +(0+@args), "\n";

    print OUT_H "void lsend_packet_$packet($new_args);\n";
    print OUT_C <<EOD;
void lsend_packet_$packet($new_args)
{
  conn_list_iterate(*dest, pconn)
    send_packet_$packet(pconn$call_args);
  conn_list_iterate_end;
}

EOD
}
