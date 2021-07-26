#!/bin/sh

. ./env.sh

# limited to the 15 characters (see "man pgrep")
NAME=failover-loop-s
pkill $NAME > /dev/null
while pkill -0 $NAME 2>/dev/null; do
    echo "wait for $NAME to stop"
    sleep 1
done

exit 0
