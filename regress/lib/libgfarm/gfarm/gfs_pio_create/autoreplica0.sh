#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
tmpf=$gftmp/foo

trap 'gfrm -f $tmpf; gfrmdir $gftmp; exit $exit_trap' $trap_sigs

# test against newly created 0 byte file

if gfmkdir $gftmp &&
   gfncopy -s 2 $gftmp &&
   $gfs_pio_test -c -w -t $tmpf
then
	exit_code=$exit_pass
fi

gfrm $tmpf
gfrmdir $gftmp
exit $exit_code
