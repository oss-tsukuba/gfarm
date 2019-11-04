#!/bin/sh

. ./regress.conf
gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

case ${GFARM_TEST_CKSUM_MISMATCH+set} in
set)	;;
*)	exit $exit_unsupported;;
esac

if
   # gfs_pio_recvfile since gfarm-2.6
   ( gfexport $* $GFARM_TEST_CKSUM_MISMATCH >/dev/null
   ) 2>&1 | grep 'checksum mismatch' >/dev/null &&

   # gfs_pio_read
   ( $gfs_pio_test $* -r -I $GFARM_TEST_CKSUM_MISMATCH >/dev/null
   ) 2>&1 | grep 'checksum mismatch' >/dev/null &&

   # gfs_pio_recvfile
   ( $gfs_pio_test $* -r -A 0,0,-1 $GFARM_TEST_CKSUM_MISMATCH >/dev/null
   ) 2>&1 | grep 'checksum mismatch' >/dev/null &&

   true

then
	exit $exit_pass
else
	exit $exit_fail
fi

