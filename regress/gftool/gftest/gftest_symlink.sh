#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfln -s $gftmp $gftmp && gftest -h $gftmp; then
    exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
