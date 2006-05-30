#!/bin/sh

. regress.conf

# prepare
if gfmkdir $base && gfmkdir $base/xxx &&
  [ x"`gfls -d $base/xxx`" = x"$base/xxx" ]; then
	:
else
	echo Failed to prepare >&2; exit 1
fi

# actual test
if gfmv $base/xxx $base/yyy &&
  [ x"`gfls -d $base/xxx`" = x"" ] &&
  [ x"`gfls -d $base/yyy`" = x"$base/yyy" ]; then
	status=0
fi

# cleanup
gfrmdir $base/yyy
gfrmdir $base
