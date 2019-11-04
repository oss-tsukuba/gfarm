#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp && gftest -f $gftmp; then
    exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
