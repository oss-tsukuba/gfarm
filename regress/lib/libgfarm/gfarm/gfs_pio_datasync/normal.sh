#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <string>
$shell $testbase/../gfs_pio_flush/write_buffered.sh $testbin/../gfs_pio_datasync/datasync $gftmp OK
exit_code=$?

gfrm $gftmp
exit $exit_code
