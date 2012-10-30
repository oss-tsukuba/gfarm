#!/usr/bin/perl

use strict;
use warnings;
use bigint;
use File::Basename qw(dirname);
use File::Path qw(make_path);
use Fcntl;

sub gfsd_local_path {
    my $inum = int($_[0]);
    my $gen = int($_[1]);
    my $dir = $_[2];

    return (sprintf("%s/data/%08X/%02X/%02X/%02X/%02X%08X%08X", $dir,
                   (($inum >> 32) & 0xffffffff),
                   (($inum >> 24) & 0xff),
                   (($inum >> 16) & 0xff),
                   (($inum >>  8) & 0xff),
                   ( $inum        & 0xff),
                   (($gen  >> 32) & 0xffffffff),
                   ( $gen         & 0xffffffff)));
}

sub name_lost_found {
    my $inum = int($_[0]);
    my $gen = int($_[1]);

    return (sprintf("%016X%016X", $inum, $gen));
}

sub make_invalid_file {
    my $path = $_[0];
    my $t;
    my $sec;
    my $min;
    my $hour;
    my $mday;
    my $mon;
    my $year;
    my $wday;
    my $yday;
    my $isdst;

    if (-e $path) {
        print "exist $path\n";
        return (0); # Don't overwrite
    }
    # O_EXCL: double check

    sysopen(my $fh, $path, O_CREAT | O_WRONLY | O_EXCL) || return (0);
    $t = time();
    ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday, $isdst)
        = localtime($t);
    printf($fh "%04d/%02d/%02d-%02d:%02d:%02d\n",
           ($year + 1900), ($mon + 1), $mday, $hour, $min, $sec);
    close($fh);

    return (1);
}

sub make_ancestor_dir {
    my $path = $_[0];
    my $dir = dirname($path);
    if (! -d $dir) {
        make_path($dir, { mode => 0700 });
    }
}

my $ARGC = $#ARGV + 1;
if ($ARGC == 0 || $ARGC > 2) {
    print "usage: $0 Gfarm_spool_directory [num]\n";
    exit(1);
}

my $spool = $ARGV[0];
my $num;
my $once;
if ($ARGC == 2) {
    $num = int($ARGV[1]);
    $once = 0;
} else {
    $num = 1;
    $once = 1;
}
my $path;
my $dir;
my $inum;
my $gen;
my $i64 = 2**64;
my $success = 0;
my $error = 0;

while ($success < $num && $error < $num) {
    $inum = int(rand(254)) + 2; # 2..255
    $gen = int(rand($i64));

    #print $inum . " " . $gen . "\n";

    $path = gfsd_local_path($inum, $gen, $spool);

    # use existing dir (inode 2..255)
    #make_ancestor_dir($path);

    if (make_invalid_file($path)) {
        $success++;
    } else {
        $error++;
    }
}

if ($once) {
    if ($success == 1) {
        print name_lost_found($inum, $gen) . "\n";
    }
} else {
    print "created: " . $success . " files\n";
}

if ($success >= 1) {
    exit(0);
} else {
    exit(1);
}
