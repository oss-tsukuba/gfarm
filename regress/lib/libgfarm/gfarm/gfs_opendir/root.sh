#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url>
$shell $testbase/opendir.sh gfarm:/
exit_code=$?

exit $exit_code
