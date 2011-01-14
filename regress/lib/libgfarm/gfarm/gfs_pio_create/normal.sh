#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if $gfs_pio_test -c -w -t $gftmp; then
	exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
