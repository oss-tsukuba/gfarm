#!/bin/sh
set -eux
f=$1
awk '
	/BEGIN$/	{ s = $2 }
	/^@@_ start/	{ t = $4 }
	/^@@~  end/	{ t = $4 - t; if (t > 4) print t, s }
' $f | sort -nr
