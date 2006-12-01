#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <character>
$shell $testbase/putline.sh $gftmp OK
exit_code=$?

gfrm $gftmp
exit $exit_code
