#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if ! gfreg $data/1byte $gftmp || [ x"`gfls $gftmp`" != x"$gftmp" ]; then
    exit $exit_unsupported
fi

$shell $testbase/gfrm-r.sh $gftmp
