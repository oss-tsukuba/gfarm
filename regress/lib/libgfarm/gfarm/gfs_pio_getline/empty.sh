#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

# arguments are <gfarm_url> <size of bytes> <return string:size of bytes>
$shell $testbase/getline.sh $gftmp 2 :1
exit_code=$?

gfrm $gftmp
exit $exit_code
