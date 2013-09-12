#!/bin/sh

. ./regress.conf

# client runs on a filesystem node

host=`hostname`
if gfsched -D $host >/dev/null 2>&1; then
	$testbase/gfs_pio_recvfile.sh -h $host
else
	exit $exit_unsupported
fi
