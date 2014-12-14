#!/bin/sh

. ./regress.conf

case ${GFARM_TEST_CKSUM_MISMATCH+set} in
set)	;;
*)	exit $exit_unsupported;;
esac

# client runs on a filesystem node

if host=`$regress/bin/get_local_gfhost $GFARM_TEST_CKSUM_MISMATCH` &&
   $regress/bin/is_client_digest_enabled
then
	$testbase/cksum_mismatch_bufsize.sh -h $host
else
	exit $exit_unsupported
fi
