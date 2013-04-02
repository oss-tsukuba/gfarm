#!/bin/sh

host=$1
TIMEOUT=60

while [ $TIMEOUT -gt 0 ] && !(gfsched -M | grep -x $host > /dev/null); do
    echo waiting for gfsd \($host\) connection [$TIMEOUT]
    sleep 1
    TIMEOUT=`expr $TIMEOUT - 1`
done

if [ $TIMEOUT -eq 0 ]; then
    exit 1
else
    exit 0
fi
