#!/bin/sh

. ./regress.conf

datafile=$data/1byte

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

test_gfreg_m()
{
    mode="$1"
    expect="$2"

    ok=0
    if gfreg -m $mode $datafile $gftmp &&
	    [ x"`gfls -l $gftmp | awk '{ print $1 }'`" = x"$expect" ]
    then
	ok=1
    fi
    gfrm $gftmp
    if [ $ok -eq 0 ]; then
        exit $exit_fail
    fi
}

test_gfreg_m 764 -rwxrw-r--
test_gfreg_m 421 -r---w---x   # readonly file

exit $exit_pass
