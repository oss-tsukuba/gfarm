#!/bin/sh

. ./regress.conf

# client runs on a (possibly) non-filesystem node

if host=`$regress/bin/get_remote_gfhost`; then
	$testbase/gfs_pio_sendfile.sh -h $host
else
	exit $exit_unsupported
fi
