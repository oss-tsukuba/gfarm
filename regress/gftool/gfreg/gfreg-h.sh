#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

host="`gfhost | head -1`"

if gfreg -h $host $data/1byte $gftmp &&
   gfexport $gftmp | cmp -s - $data/1byte &&
   gfwhere $gftmp | awk 'NR>1{if ($2 == "'$host'") exit 0; else exit 1}'; then
	exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
