#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# 0x00000401 is GFARM_FILE_WRONLY|GFARM_FILE_TRUNC
if ! $testbin/../gfs_pio_create/create $gftmp 0x00000401 0644; then
    exit $exit_unsupported
fi

# argument is <gfarm_url>
$shell $testbase/unlink.sh $gftmp
exit_code=$?

if [ $exit_code != $exit_pass ]; then
    gfrm $gftmp
fi
exit $exit_code
