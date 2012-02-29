#!/bin/sh

. ./env.sh

gfrm -f $GF_TMPF\*
gfrm -f $GF_TMPF_SLNK\*
gfrmdir $GF_TMPD.0 2>/dev/null
gfrm -f $GF_TMPD/\*
gfrmdir $GF_TMPD 2>/dev/null

