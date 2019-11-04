#!/bin/sh

. ./regress.conf

# client does not run on the filesystem node

if host=`$regress/bin/get_remote_gfhost` &&
   $regress/bin/is_digest_enabled
then
	$testbase/cksum_simultaneous.sh -h $host
else
	exit $exit_unsupported
fi
