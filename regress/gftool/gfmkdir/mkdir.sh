#!/bin/sh

. regress.conf

if gfmkdir $base && [ x"`gfls -d $base`" = x"$base" ]; then
	status=0
fi
gfrmdir $base
