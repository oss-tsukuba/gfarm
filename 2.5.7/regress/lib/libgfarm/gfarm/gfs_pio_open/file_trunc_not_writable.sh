#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

if $regress/bin/am_I_gfarmroot; then
  exit $exit_unsupported
fi

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp && gfchmod 0444 $gftmp
then
   if $gfs_pio_test -t $* $gftmp 2>$localtmp; then
	:
   elif [ $? -eq 2 ] &&
	echo "gfs_pio_open: permission denied" | cmp - $localtmp
   then
	exit_code=$exit_pass
   fi
fi

gfrm $gftmp
rm -f $localtmp
exit $exit_code
