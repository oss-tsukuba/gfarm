#!/bin/sh

. ./env.sh

STOPPED=0

stop() {
   STOPPED=1
}

trap stop 1 2 15

TMLIMIT=$1
if [ "$1" = "" ]; then
    echo time limit is not specified
    exit 1
fi

tm0=`date +%s`
SLEEP=15

while [ 1 ]; do
	tm=`date +%s`
	if [ `expr $tm - $tm0` -ge $TMLIMIT ]; then
		exit
	fi

	./gfmd-failover.sh >/dev/null
	count=0
	while [ $count -lt $SLEEP ]; do
	    [ $STOPPED -eq 1 ] && exit 0
	    sleep 1
	    count=`expr $count + 1`
	done
done
