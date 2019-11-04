#!/bin/sh

. ./regress.conf

case ${GFARM_TEST_CKSUM_MISMATCH+set} in
set)	;;
*)	exit $exit_unsupported;;
esac

# client does not run on the filesystem node

if host=`$regress/bin/get_remote_gfhost $GFARM_TEST_CKSUM_MISMATCH`
then
	$testbase/cksum_no_check.sh -h $host
else
	exit $exit_unsupported
fi
