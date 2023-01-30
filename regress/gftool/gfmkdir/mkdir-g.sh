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

trap 'gfrm -rf $gftmp; exit $exit_trap' $trap_sigs

test_gfmkdir_g()
{
    group=$1

    if gfmkdir -g $group $gftmp &&
	    [ x"`gfls -ld $gftmp | awk '{ print $4 }'`" = x"$group" ]
    then
	exit_code=$exit_pass
    fi
    gfrm -rf $gftmp
}

test_gfmkdir_g $g1
test_gfmkdir_g $g2

exit $exit_code
