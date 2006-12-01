#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp &&
    [ `gfls -l $gftmp | awk '{ printf "%d%s", $4, $8}'` = \
	1`echo $gftmp | sed s@\.\/gfarm\:@@` ]; then
    exit_code=$exit_pass
fi

gfrm -f $gftmp
exit $exit_code
