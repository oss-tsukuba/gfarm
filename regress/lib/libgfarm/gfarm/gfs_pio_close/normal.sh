#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

$shell $testbase/close.sh $gftmp
exit_code=$?

gfrm $gftmp
exit $exit_code
