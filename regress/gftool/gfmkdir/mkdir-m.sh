#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

test_gfmkdir_m()
{
    mode="$1"
    expect="$2"

    if gfmkdir -m $mode $gftmp
       [ x"`gfls -ld $gftmp | awk '{ print $1 }'`" = x"$expect" ]; then
	exit_code=$exit_pass
    fi
    gfrmdir $gftmp
}

test_gfmkdir_m 755 drwxr-xr-x
test_gfmkdir_m 712 drwx--x-w-

exit $exit_code
