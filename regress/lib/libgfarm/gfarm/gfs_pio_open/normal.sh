#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# 0x0 is GFARM_FILE_RDONLY
$shell $testbase/open.sh $gftmp 0x0
exit_code=$?

gfrm $gftmp
exit $exit_code
