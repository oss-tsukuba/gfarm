#!/bin/sh

. ./env.sh

gfrm -f $GF_TMPF.\*

echo ABCDEFGHIJKLMNOPQRSTUVWXYZ > $TMPF
gfreg $TMPF $GF_TMPF

rm -f $TMPF

