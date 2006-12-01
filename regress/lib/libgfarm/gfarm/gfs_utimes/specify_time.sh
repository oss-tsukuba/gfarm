#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# 0x00000401 is GFARM_FILE_WRONLY|GFARM_FILE_TRUNC
if ! $testbin/../gfs_pio_create/create $gftmp 0x00000401 0644; then
    exit $exit_unsupported
fi

# arguments are  <gfarm_url> <atime_sec> <atime_nsec> <mtime_sec> <mtime_nsec>
$shell $testbase/utimes.sh $gftmp 0 1111111111 2222222222 3333333333
exit_code=$?

gfrm $gftmp
exit $exit_code
