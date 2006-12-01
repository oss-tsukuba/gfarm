#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <string> <length>
$shell $testbase/truncate.sh $gftmp 1234567890 2
exit_code=$?

gfrm $gftmp
exit $exit_code
