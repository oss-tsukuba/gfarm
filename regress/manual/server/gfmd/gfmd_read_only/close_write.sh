#!/bin/sh

. ./regress.conf

SLEEP_TIME=3
SLEEP_TIME_FOR_GFSD_RETRY=60 # ospool_server_read_only_retry_interval
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

$gfs_pio_test -cw -O -F -P $SLEEP_TIME <$data/65byte $gftmp &
sleep 1

gfstatus -Mm 'read_only enable'
case `gfls -l $gftmp | awk '{print $5}'` in
0)	:;;
*)	gfstatus -Mm 'read_only disable'
	exit_code=$exit_fail
	exit;;
esac

sleep $SLEEP_TIME

gfstatus -Mm 'read_only disable'
case `gfls -l $gftmp | awk '{print $5}'` in
0)	:;;
*)	exit_code=$exit_fail
	exit;;
esac

sleep $SLEEP_TIME_FOR_GFSD_RETRY

case `gfls -l $gftmp | awk '{print $5}'` in
65)	:;;
*)	exit_code=$exit_fail
	exit;;
esac

exit_code=$exit_pass
