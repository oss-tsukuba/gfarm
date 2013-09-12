#!/bin/sh

. ./regress.conf

# client runs on a (possibly) non-filesystem node

host=`hostname`

if host=`gfsched -D $host 2>/dev/null`; then
	host=`gfsched | awk '$0 != "'$host'" { print $0; exit }'`
else
	host=`gfsched | head -1`
fi
$testbase/gfs_pio_sendfile.sh -h $host
