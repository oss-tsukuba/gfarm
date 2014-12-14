#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if
   # gfs_pio_write
   $gfs_pio_test $* -ctw -O $gftmp <$data/65byte &&
   $regress/bin/is_cksum_same $gftmp $data/65byte &&
   gfrm -f $gftmp &&

   # gfs_pio_write, no calculation
   $gfs_pio_test $* -ctw -S 1 -O $gftmp <$data/65byte &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfrm -f $gftmp &&

   # gfs_pio_sendfile
   $gfs_pio_test $* -ctw -Y 0,0,-1 $gftmp <$data/65byte &&
   $regress/bin/is_cksum_same $gftmp $data/65byte &&
   gfrm -f $gftmp

   # gfs_pio_sendfile, no calculation
   $gfs_pio_test $* -ctw -Y 1,0,-1 $gftmp <$data/65byte &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfrm -f $gftmp &&

   # gfs_pio_truncate to 0, no calculation
   $gfs_pio_test $* -ctw -O -T 0 $gftmp <$data/65byte &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfrm -f $gftmp &&

   # gfs_pio_truncate to shorter size, no calculation
   $gfs_pio_test $* -ctw -O -T 64  $gftmp <$data/65byte &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfrm -f $gftmp &&

   # gfs_pio_truncate to just same size
   $gfs_pio_test $* -ctw -O -T 65  $gftmp <$data/65byte &&
   $regress/bin/is_cksum_same $gftmp $data/65byte &&
   gfrm -f $gftmp &&

   # gfs_pio_truncate to longer size, no calculation
   $gfs_pio_test $* -ctw -O -T 66  $gftmp <$data/65byte &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfrm -f $gftmp &&

   true

then
    exit_code=$exit_pass
fi

gfrm -f $gftmp
rm -f $localtmp
exit $exit_code
