#!/bin/sh

. ./env.sh

TMLIMIT=$1
if [ "$1" = "" ]; then
    echo time limit is not specified
    exit 1
fi

tm0=`date +%s`

while [ 1 ]; do
	tm=`date +%s`
	if [ `expr $tm - $tm0` -ge $TMLIMIT ]; then
		exit
	fi

	./gfmd-failover.sh >/dev/null
	sleep 15
done
