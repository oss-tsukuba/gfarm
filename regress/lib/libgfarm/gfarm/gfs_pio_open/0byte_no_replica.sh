#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

# create a 0-byte file without any replica,
# and access it

if

   # SF.net #957 - read-only access causes EBADF
   $gfs_pio_test -ct $gftmp &&
   gfexport $gftmp >$localtmp &&
   cmp -s $localtmp /dev/null &&

   # write possible?
   gfrm -f $gftmp &&
   $gfs_pio_test -ct $gftmp &&
   $gfs_pio_test -wa -O $gftmp <$data/1byte &&
   gfexport $gftmp >$localtmp &&
   cmp -s $localtmp $data/1byte &&

   true
then
    exit_code=$exit_pass
fi

gfrm -f $gftmp
rm -f $localtmp
exit $exit_code
