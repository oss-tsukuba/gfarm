#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp &&
    [ `gfstat $gftmp | awk '/Size:/ { print $2 }'` == "1" ]; then
    exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
