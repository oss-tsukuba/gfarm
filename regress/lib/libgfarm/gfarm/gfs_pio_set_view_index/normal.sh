#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are gfarm_url fragment_number fragment_index host flags
$shell $testbase/set_view_index.sh $gftmp 1 0 NULL 0
exit_code=$?

gfrm $gftmp
exit $exit_code
