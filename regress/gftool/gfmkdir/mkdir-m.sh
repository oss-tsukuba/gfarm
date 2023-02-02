#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

test_gfmkdir_m()
{
    mode="$1"
    expect="$2"

    ok=0
    if gfmkdir -m $mode $gftmp &&
       [ x"`gfls -ld $gftmp | awk '{ print $1 }'`" = x"$expect" ]; then
	ok=1
    fi
    gfrmdir $gftmp
    if [ $ok -eq 0 ]; then
        exit $exit_fail
    fi
}

test_gfmkdir_m 755 drwxr-xr-x
test_gfmkdir_m 712 drwx--x-w-

exit $exit_pass
