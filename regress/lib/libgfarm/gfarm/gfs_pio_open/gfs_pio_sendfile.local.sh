#!/bin/sh

. ./regress.conf

# client runs on a filesystem node

if host=`$regress/bin/get_local_gfhost`; then
	$testbase/gfs_pio_sendfile.sh -h $host
else
	exit $exit_unsupported
fi
