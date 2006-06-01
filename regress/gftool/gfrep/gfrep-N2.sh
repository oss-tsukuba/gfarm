#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp &&
   gfrep -N 2 $gftmp &&
   gfwhere $gftmp | awk '{ if (NF == 3) exit 0; else exit 1}'
then
	exit_code=$exit_pass
fi

exit $exit_code
