#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

# argument is <string>
$shell $testbase/STRINGLIST_ELEM.sh OK
