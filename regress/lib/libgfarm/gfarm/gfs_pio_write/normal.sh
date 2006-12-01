#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <string> <size of bytes>
$shell $testbase/write.sh $gftmp OK 2 
exit_code=$?

gfrm $gftmp
exit $exit_code
