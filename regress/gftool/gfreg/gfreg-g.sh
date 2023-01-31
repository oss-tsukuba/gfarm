#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarmroot; then
	:
else
	exit $exit_unsupported
fi
if [ `gfgroup | sed 2q | wc -l` -ne 2 ]; then
	exit $exit_unsupported
fi

g1=`gfgroup | sed 1q`
g2=`gfgroup | sed -n 2p`

datafile=$data/1byte

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

test_gfreg_g()
{
    group="$1"

    ok=0
    if gfreg -g $group $datafile $gftmp &&
	    [ x"`gfls -l $gftmp | awk '{ print $4 }'`" = x"$group" ]
    then
	ok=1
    fi
    gfrm $gftmp
    if [ $ok -eq 0 ]; then
        exit $exit_fail
    fi
}

test_gfreg_g $g1
test_gfreg_g $g2

exit $exit_pass
