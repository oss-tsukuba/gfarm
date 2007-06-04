#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

# arguments are <string1> <string2>
$shell $testbase/STRINGLIST_STRARRAY.sh 1st 2nd
