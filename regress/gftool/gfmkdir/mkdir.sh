#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp && [ x"`gfls -d $gftmp`" = x"$gftmp" ]; then
	exit_code=$exit_pass
fi

gfrmdir $gftmp
exit $exit_code
