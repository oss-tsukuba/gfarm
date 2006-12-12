#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfreg -N 2 -I 0 $data/0byte $gftmp &&
   gfreg -N 2 -I 1 $data/1byte $gftmp &&
   gfexport $gftmp | cmp -s - $data/1byte
then
    exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
