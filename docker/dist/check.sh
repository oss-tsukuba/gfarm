#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

gfdf
gfhost -lv
gfmdhost -l

NOTHEALTHY=0
for h in $(gfmdhost -l | awk '$1 !~ /^+/ {print $6}')
do
	echo $h: not synchronize, restart
	ssh $h sudo systemctl restart gfmd
	NOTHEALTHY=1
done
[ $NOTHEALTHY = 0 ] || {
	sleep 1
	gfmdhost -l
}
status=0
echo Done
