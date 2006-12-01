#!/bin/sh

. ./regress.conf

#trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# 0x00000401 is GFARM_FILE_WRONLY|GFARM_FILE_TRUNC
if ! $testbin/../gfs_pio_create/create $gftmp 0x00000401 0666; then
    exit $exit_unsupported
fi

# arguments are <gfarm_url of a directory> <filename portion>
$shell $testbase/readdir.sh `dirname $gftmp` "..."`basename $gftmp`
exit_code=$?

gfrm $gftmp
exit $exit_code
