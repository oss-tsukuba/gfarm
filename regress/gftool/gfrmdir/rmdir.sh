#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   gfrmdir $gftmp && [ x"`gfls -d $gftmp`" = x"" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
