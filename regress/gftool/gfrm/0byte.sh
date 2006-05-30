#!/bin/sh

. regress.conf

if gfreg data/0byte $base && [ x"`gfls $base`" = x"$base" ] &&
   [ x"`gfls -l $base | awk '{print $4}'`" = x"0" ] &&
   gfrm $base && [ x"`gfls $base`" = x"" ]; then
	status=0
fi
