#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

# argument is <string>
$shell $testbase/stringlist_add.sh OK
