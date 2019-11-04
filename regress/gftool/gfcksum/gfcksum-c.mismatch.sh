#!/bin/sh

. ./regress.conf

case ${GFARM_TEST_CKSUM_MISMATCH+set} in
set)	;;
*)	exit $exit_unsupported;;
esac

if ( gfcksum -c $GFARM_TEST_CKSUM_MISMATCH >/dev/null ) 2>&1 |
	grep ' differs$' >/dev/null
then
	exit $exit_pass
else
	exit $exit_fail
fi
