#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <character>
$shell $testbase/putc.sh $gftmp 0x41
exit_code=$?

gfrm $gftmp
exit $exit_code
