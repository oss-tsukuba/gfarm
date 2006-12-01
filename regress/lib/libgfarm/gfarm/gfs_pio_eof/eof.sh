#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

$shell $testbase/pio_eof.sh $gftmp read EOF
exit_code=$?

gfrm $gftmp
exit $exit_code
