#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

# arguments are gfarm_url flags
$shell $testbase/set_view_local.sh $gftmp 0x01000000
exit_code=$?

gfrm $gftmp
exit $exit_code
