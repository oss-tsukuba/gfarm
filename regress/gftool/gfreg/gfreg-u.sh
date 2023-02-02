#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarmroot; then
	:
else
	exit $exit_unsupported
fi
if [ `gfuser | sed 2q | wc -l` -ne 2 ]; then
	exit $exit_unsupported
fi

me=`gfwhoami`
user=`gfuser | sed 1q`
if [ x"$user" = x"$me" ]; then
	user=`gfuser | sed -n 2p`
fi

datafile=$data/1byte

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

test_gfreg_u()
{
    user="$1"

    ok=0
    if gfreg -u $user $datafile $gftmp &&
	    [ x"`gfls -l $gftmp | awk '{ print $3 }'`" = x"$user" ]
    then
	ok=1
    fi
    gfrm $gftmp
    if [ $ok -eq 0 ]; then
        exit $exit_fail
    fi
}

test_gfreg_u $user
test_gfreg_u $me

exit $exit_pass
