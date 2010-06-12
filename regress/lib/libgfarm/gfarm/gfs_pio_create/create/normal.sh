#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# 0x00000401 is GFARM_FILE_WRONLY|GFARM_FILE_TRUNC
$shell $testbase/create.sh $gftmp 0x00000401 0666
exit_code=$?

gfrm $gftmp
exit $exit_code
