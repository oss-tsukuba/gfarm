#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <mode>
$shell $testbase/mkdir.sh $gftmp 0755
exit_code=$?

gfrmdir $gftmp
exit $exit_code
