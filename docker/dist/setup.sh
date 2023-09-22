#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

for h in c1 c2 c3 c4 c5 c6 c7 c8
do
	echo $h
done > ~/.nodelist

status=0
