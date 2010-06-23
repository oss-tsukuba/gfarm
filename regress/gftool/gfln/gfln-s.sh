#!/bin/sh

. ./regress.conf

srcpath=aaaaa

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if gfln -s $srcpath $gftmp &&
   gfls -l $gftmp | grep " -> $srcpath"'$' >/dev/null; then
	exit_code=$exit_pass
fi

gfrm -f $gftmp
exit $exit_code
