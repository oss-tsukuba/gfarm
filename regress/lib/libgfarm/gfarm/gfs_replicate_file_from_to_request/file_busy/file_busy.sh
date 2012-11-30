#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; gfxattr -r $gftop gfarm.ncopy; rm -f $localtmp; exit $exit_trap' $trap_sigs

GFARM_FILE_RDWR=2	# from <gfarm/gfs.h>

gfsched -w >$localtmp

if [ `sed 2q $localtmp | wc -l` -ne 2 ]; then
    rm -f $localtmp
    exit $exit_unsupported
fi
srchost=`sed -n 1p $localtmp`
dsthost=`sed -n 2p $localtmp`

# remove the effect of gfarm.ncopy in the root directory
gfncopy -s 1 $gftop

if gfreg -h $srchost $data/1byte $gftmp &&
   $testbin/file_busy $gftmp $GFARM_FILE_RDWR $dsthost 0 2>&1 |
	fgrep 'file busy' >/dev/null
then
    exit_code=$exit_pass
fi

gfrm $gftmp
gfxattr -r $gftop gfarm.ncopy
rm -f $localtmp
exit $exit_code
