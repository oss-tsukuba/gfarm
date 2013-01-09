#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   gfchmod $1 $gftmp &&
   [ x"`gfls -ld $gftmp | awk '{ print $1 }'`" = x$2 ]; then 
	exit_code=$exit_pass
fi

gfrmdir $gftmp
exit $exit_code
