#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if gfreg /bin/echo $gftmp && [ x"`gfrun $gftmp OK`" = x"OK" ]; then
	exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
