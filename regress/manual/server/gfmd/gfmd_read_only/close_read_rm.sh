#!/bin/sh

. ./regress.conf

SLEEP_TIME=3
gfs_pio_test=`dirname $testbin`		# regress/manual/server/gfmd
gfs_pio_test=`dirname $gfs_pio_test`	# regress/manual/server
gfs_pio_test=`dirname $gfs_pio_test`	# regress/manual
gfs_pio_test=`dirname $gfs_pio_test`	# regress/
gfs_pio_test=$gfs_pio_test/lib/libgfarm/gfarm/gfs_pio_test/gfs_pio_test

if $regress/bin/am_I_gfarmadm; then
  :
else
  exit $exit_unsupported
fi

exit_code=$exit_trap
trap 'gfrm -rf $gftmp; exit $exit_code' 0 $trap_sigs

if gfreg $data/65byte $gftmp; then
	:
else
	exit_code=$exit_fail
	exit
fi

$gfs_pio_test -r -I -P $SLEEP_TIME $gftmp >/dev/null &
sleep 1
if gfrm $gftmp; then
	:
else
	exit_code=$exit_fail
	exit
fi

gfstatus -Mm 'read_only enable'

sleep $SLEEP_TIME

gfstatus -Mm 'read_only disable'
exit_code=$exit_pass

# NOTE:
# orphan inode and replica remain
# You should try gfmd restart and gfsd spool_check to see the result
