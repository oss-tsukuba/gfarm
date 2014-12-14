#!/bin/sh

. ./regress.conf
gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

case ${GFARM_TEST_CKSUM_MISMATCH+set} in
set)	;;
*)	exit $exit_unsupported;;
esac

filesize=`gfstat $GFARM_TEST_CKSUM_MISMATCH | awk '$1 == "Size:" {print $2}'`
bufsize=`gfstatus client_file_bufsize`
[ $filesize -le $bufsize ] && exit $exit_unsupported

bufsize1=`expr $bufsize + 1`

# gfs_pio_read, then gfs_pio_recvfile
( ( $gfs_pio_test -r -R $bufsize -A $bufsize,0,-1 $GFARM_TEST_CKSUM_MISMATCH >/dev/null ) 2>&1 | grep 'checksum mismatch' >/dev/null ) || exit $exit_fail

# gfs_pio_read, gap, then gfs_pio_recvfile -> DOES NOT DETECT
( ( $gfs_pio_test -r -R $bufsize -A $bufsize1,0,-1 $GFARM_TEST_CKSUM_MISMATCH >/dev/null ) 2>&1 | grep 'checksum mismatch' >/dev/null ) && exit $exit_fail

# gfs_pio_recvfile, then gfs_pio_read
( ( $gfs_pio_test -r -A 0,0,$bufsize -S $bufsize -I $GFARM_TEST_CKSUM_MISMATCH > /dev/null ) 2>&1 | grep 'checksum mismatch' >/dev/null ) || exit $exit_fail

# gfs_pio_recvfile, gap, then gfs_pio_read -> DOES NOT DETECT
( ( $gfs_pio_test -r -A 0,0,$bufsize -S $bufsize1 -I $GFARM_TEST_CKSUM_MISMATCH > /dev/null ) 2>&1 | grep 'checksum mismatch' >/dev/null ) && exit $exit_fail

exit 0
