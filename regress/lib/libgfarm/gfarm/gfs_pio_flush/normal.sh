#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <string>
$shell $testbase/write_buffered.sh $testbin/flush $gftmp OK
exit_code=$?

gfrm $gftmp
exit $exit_code
