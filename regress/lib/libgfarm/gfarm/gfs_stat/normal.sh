#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <string>
$shell $testbase/stat.sh $gftmp ABC
exit_code=$?

gfrm $gftmp
exit $exit_code
