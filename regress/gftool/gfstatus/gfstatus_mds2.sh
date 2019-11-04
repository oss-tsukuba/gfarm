#!/bin/sh

. ./regress.conf

dir=gfarm://$GFARM_TEST_MDS2$gftmp

if [ "$GFARM_TEST_MDS2" = "" ]; then
	echo GFARM_TEST_MDS2 is not set
	exit $exit_unsupported
fi

if ! gfstatus -P gfarm://$GFARM_TEST_MDS2; then
	exit $exit_fail
fi

exit $exit_pass
