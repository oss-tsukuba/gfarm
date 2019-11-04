#!/bin/sh

. ./regress.conf

case ${GFARM_TEST_CKSUM_MISMATCH+set} in
set)	;;
*)	exit $exit_unsupported;;
esac

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

( ( $gfs_pio_test $* -r -I $GFARM_TEST_CKSUM_MISMATCH >/dev/null ) 2>&1 |
	grep 'checksum mismatch' >/dev/null ) || exit $exit_unsupported

# just open.  no read/write
( ( $gfs_pio_test $* -r $GFARM_TEST_CKSUM_MISMATCH >/dev/null ) 2>&1 |
	grep 'checksum mismatch' >/dev/null ) && exit $exit_fail

# partial read by gfs_pio_read()
( ( $gfs_pio_test $* -r -S1 -I $GFARM_TEST_CKSUM_MISMATCH >/dev/null
  ) 2>&1 | grep 'checksum mismatch' >/dev/null ) && exit $exit_fail

# partial read by gfs_pio_recvfile()
( ( $gfs_pio_test $* -r -A 1,0,-1 $GFARM_TEST_CKSUM_MISMATCH >/dev/null
  ) 2>&1 | grep 'checksum mismatch' >/dev/null ) && exit $exit_fail

exit $exit_pass
