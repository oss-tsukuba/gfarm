#!/bin/sh

. ./env.sh

get_gfsd

gfrm -f $GF_TMPF.\*

echo ABCDEFGHIJKLMNOPQRSTUVWXYZ > $TMPF
gfreg -h $GFSD0 $TMPF $GF_TMPF
echo test | gfxattr -s $GF_TMPF user.test
echo "<a>x</a>" | gfxattr -x -s $GF_TMPF user.testx

gfln -s $GF_TMPF $GF_TMPF_SLNK

gfmkdir $GF_TMPD
gfreg $TMPF $GF_TMPD/1
gfreg $TMPF $GF_TMPD/2
gfreg $TMPF $GF_TMPD/3

rm -f $TMPF

