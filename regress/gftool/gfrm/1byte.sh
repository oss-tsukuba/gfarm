#!/bin/sh

. regress.conf

if gfreg data/1byte $base && [ x"`gfls $base`" = x"$base" ] &&
   [ x"`gfls -l $base | awk '{print $4}'`" = x"1" ] &&
   gfrm $base && [ x"`gfls $base`" = x"" ]; then
	status=0
fi
