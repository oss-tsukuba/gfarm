#!/bin/sh

. ./regress.conf

[ `gfsched -w | wc -l` -ge 2 ] || exit $exit_unsupported

WAIT_TIME=1

gfs_pio_test=$testbin/../gfs_pio_test/gfs_pio_test
tmpf=$gftmp/foo

trap 'gfrm -f $tmpf; gfrmdir $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   echo 2 | gfxattr -s $gftmp gfarm.ncopy &&
   echo 123456789 | $gfs_pio_test -c -w -W10 $tmpf &&
   sleep $WAIT_TIME && [ `gfstat $tmpf | awk '/Ncopy/{print $NF}'` -eq 2 ]
then
	exit_code=$exit_pass
fi

gfrm $tmpf
gfrmdir $gftmp
exit $exit_code
