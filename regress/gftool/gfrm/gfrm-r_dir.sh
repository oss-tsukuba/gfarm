#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if ! gfmkdir $gftmp || [ x"`gfls -d $gftmp`" != x"$gftmp" ] ||
   ! gfreg $data/1byte $gftmp/1byte ||
   [ x"`gfls $gftmp/1byte`" != x"$gftmp/1byte" ]; then
    exit $exit_unsupported
fi

$shell $testbase/gfrm-r.sh $gftmp
