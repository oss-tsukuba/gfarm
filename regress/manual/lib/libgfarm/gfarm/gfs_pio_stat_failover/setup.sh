#/bin/sh

TMPF=/tmp/gfs_pio_stat_failover.tmp
GF_TMPF=$TMPF

gfrm -f $GF_TMPF.\*

echo ABCDEFGHIJKLMNOPQRSTUVWXYZ > $TMPF
gfreg $TMPF $GF_TMPF

rm -f $TMPF

