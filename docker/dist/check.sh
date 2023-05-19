#!/bin/sh
set -eu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

cmd() {
	echo [$*]
	$*
}

cmd gfdf
cmd gfhost -lv
cmd gfmdhost -l

NOTHEALTHY=0
for h in $(gfmdhost -l | awk '$1 !~ /^\+/ {print $6}')
do
	echo $h: not synchronize, restart
	ssh $h sudo systemctl restart gfmd
	NOTHEALTHY=1
done
[ $NOTHEALTHY = 0 ] || {
	echo not healthy, check again
	sleep 1
	cmd gfmdhost -l
}
status=0
