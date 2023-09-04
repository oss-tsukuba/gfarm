#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

for d in /etc /usr/local/etc
do
	[ -f $d/unconfig-gfarm.sh ] && break
done
[ -f $d/unconfig-gfarm.sh ]

for UNCONFIG in $d/unconfig-gfsd.sh $d/unconfig-gfarm.sh
do
	gfarm-prun -a -p "[ -f $UNCONFIG ] && sudo $UNCONFIG -f || :"
done

gfarm-prun -a -p mv .gfarm2rc .gfarm2rc.bak

status=0
echo Done
