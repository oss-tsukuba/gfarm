#!/bin/sh

. ./regress.conf

p1=$gftmp/p1
p2=$gftmp/p2

trap 'gfrmdir $p1/sub $p2/sub $p1 $p2 $gftmp; exit $exit_trap' $trap_sigs

gfinum()
{
	gfls -lid $1 | awk '{print $1}'
}

gfmkdir $gftmp $p1 $p2 $p1/sub
o=`gfinum $p1/sub/..`
gfmv $p1/sub $p2/sub
n=`gfinum $p2/sub/..`

if [ x"$o" = x"`gfinum $p1`" ] &&
   [ x"$n" = x"`gfinum $p2`" ]
then
	exit_code=$exit_pass
fi

gfrmdir $p1/sub $p2/sub $p1 $p2 $gftmp
exit $exit_code
