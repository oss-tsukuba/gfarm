#!/bin/sh

. ./regress.conf
gftmpfile=$gftmp/file
exit_code=$exit_trap

trap 'gfrm -rf $gftmp; rm -f $localtmp; exit $exit_code' 0 $trap_sigs

GFARM_FILE_RDWR=2	# from <gfarm/gfs.h>

gfsched -w >$localtmp

if [ `sed 2q $localtmp | wc -l` -ne 2 ]; then
    exit_code=$exit_unsupported
    exit
fi
srchost=`sed -n 1p $localtmp`
dsthost=`sed -n 2p $localtmp`

if
    # disable automatic replication
    gfmkdir $gftmp &&
    gfncopy -s 1 $gftmp &&

    gfreg -h $srchost $data/1byte $gftmpfile &&
    $testbin/file_busy $gftmpfile $GFARM_FILE_RDWR $dsthost 0 2>&1 |
	fgrep 'file busy' >/dev/null
then
    exit_code=$exit_pass
else
    exit_code=$exit_fail
fi
