#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp; then
    if $gfs_pio_test -ce $gftmp 2> $localtmp; then
        echo >&2 "gfs_pio_test -ce must fail"
    else
        if grep "already exists" $localtmp; then
            exit_code=$exit_pass
        else
            echo >&2 "unexpected error"
            cat $localtmp
        fi
    fi
else
    echo >&2 "gfreg failed"
fi

gfrm $gftmp
rm -f $localtmp
exit $exit_code
