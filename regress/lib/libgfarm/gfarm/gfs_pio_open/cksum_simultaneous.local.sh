#!/bin/sh

. ./regress.conf

# client runs on a filesystem node

if host=`$regress/bin/get_local_gfhost` &&
   $regress/bin/is_digest_enabled &&
   $regress/bin/is_client_digest_enabled
then
	$testbase/cksum_simultaneous.sh -h $host
else
	exit $exit_unsupported
fi
