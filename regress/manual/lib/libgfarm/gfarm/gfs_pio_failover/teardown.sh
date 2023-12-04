#/bin/sh

: ${srcdir:=.}

. ${srcdir}/env.sh

gfrm -f $GF_TMPF.\*
gfrmdir $GF_TMPD.0 2>/dev/null
