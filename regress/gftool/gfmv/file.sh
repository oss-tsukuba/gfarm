#!/bin/sh

. regress.conf

# prepare
if gfmkdir $base && gfreg data/1byte $base/1byte &&
   gfexport $base/1byte | cmp -s - data/1byte; then
	:
else
	echo Failed to prepare >&2; exit 1
fi

# actual test
if gfmv $base/1byte $base/xxx &&
   gfexport $base/xxx | cmp -s - data/1byte; then
	status=0
fi

# cleanup
gfrm $base/xxx
gfrmdir $base

