#!/bin/sh

. ./env.sh

NAME=failover-loop-start
pkill $NAME > /dev/null
while pkill -0 $NAME 2>/dev/null; do
    echo "wait for $NAME to stop"
    sleep 1
done

exit 0
