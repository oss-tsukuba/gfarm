#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# argument is <gfarm_url>
$shell $testbase/realpath.sh $gftop
exit_code=$?

exit $exit_code
