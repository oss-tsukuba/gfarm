#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <follow gfa_pio_getc()?>
$shell $testbase/ungetc.sh $gftmp NOT_FOLLOW_GETC
exit_code=$?

gfrm $gftmp
exit $exit_code
