#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <buffer size> <returned message> <returned buffer size> <read length> <delimiter> <delimlen>
$shell $testbase/readdelim.sh $gftmp 0 "" 1 1 '\n' 1
exit_code=$?

gfrm $gftmp
exit $exit_code
