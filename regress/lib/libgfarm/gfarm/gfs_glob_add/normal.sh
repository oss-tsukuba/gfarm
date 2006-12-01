#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <type> [<type> ...]
$shell $testbase/glob_add.sh 4 8 0
