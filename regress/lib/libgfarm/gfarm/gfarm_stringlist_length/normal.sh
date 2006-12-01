#!/bin/sh

. ./regress.conf

trap '$gftmp; exit $exit_trap' $trap_sigs

# argument is <string>
$shell $testbase/stringlist_length.sh OK
