#!/bin/sh

. regress.conf

if gfreg data/0byte $base && [ x"`gfls $base`" = x"$base" ] &&
   [ x"`gfls -l $base | awk '{print $4}'`" = x"0" ] &&
   gfexport $base | cmp -s - data/0byte; then
	status=0
fi
gfrm $base
