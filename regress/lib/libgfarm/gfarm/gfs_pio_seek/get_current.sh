#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

seek_cur=1

# arguments are gfarm_url offset whence return_position_or_not
$shell $testbase/seek.sh $gftmp 0 $seek_cur RETURN_RESULT 0
exit_code=$?

gfrm $gftmp
exit $exit_code
