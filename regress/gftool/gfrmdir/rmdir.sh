#!/bin/sh

. regress.conf

if gfmkdir $base && [ x"`gfls -d $base`" = x"$base" ] &&
   gfrmdir $base && [ x"`gfls -d $base`" = x"" ]; then
	status=0
fi
