#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp &&
   gfchmod 755 $gftmp &&
   [ x"`gfls -l $gftmp | awk '{ print $1 }'`" = x"-rwxr-xr-x" ]; then 
	exit_code=$exit_pass
fi

gfrm -f $gftmp
exit $exit_code
