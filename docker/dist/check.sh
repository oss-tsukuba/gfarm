#!/bin/sh
set -xeu

gfdf
gfhost -lv
gfmdhost -l

NOTHEALTHY=0
for h in c2 c3
do
	gfmdhost -l | grep $h | grep ^+ > /dev/null || {
		echo $h: not synchronize, restart
		ssh $h sudo systemctl restart gfmd
		NOTHEALTHY=1
	}
done
[ $NOTHEALTHY = 1 ] && {
	sleep 1
	gfmdhost -l
} || :
