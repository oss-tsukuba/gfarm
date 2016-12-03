#!/bin/sh

. ./regress.conf

SLEEP_TIME=3
gfs_pio_test=`dirname $testbin`		# regress/gftool/
gfs_pio_test=`dirname $gfs_pio_test`	# regress/
gfs_pio_test=$gfs_pio_test/lib/libgfarm/gfarm/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if host=`gfsched -n 1 -w`; then
  if gfreg -h $host $data/1byte $gftmp; then
    $gfs_pio_test -h $host -wa -O -P$SLEEP_TIME $gftmp <$data/1byte &
    sleep 1
    if gflsof -WG | grep "^${host}\$" >/dev/null; then
      exit_code=$exit_pass
    fi
  fi
fi

gfrm -f $gftmp
exit $exit_code
