#!/bin/sh

. ./regress.conf

case ${GFARM_TEST_CKSUM_MISMATCH+set} in
set)	;;
*)	exit $exit_unsupported;;
esac

# may have been moved to lost+found
gfstat -r $GFARM_TEST_CKSUM_MISMATCH ||
	exit $exit_unsupported

if ( gfcksum -c $GFARM_TEST_CKSUM_MISMATCH >/dev/null ) 2>&1 |
	grep ' differs$' >/dev/null
then
	exit $exit_pass
else
	exit $exit_fail
fi
