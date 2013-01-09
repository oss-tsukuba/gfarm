#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfreg $data/1byte $gftmp &&
   gfchmod 644 $gftmp &&
   [ x"`gfls -l $gftmp | awk '{ print $1 }'`" = x"-rw-r--r--" ]; then 
	exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
