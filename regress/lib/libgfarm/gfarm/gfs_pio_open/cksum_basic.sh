#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
gftmpfile=$gftmp/file

exit_code=$exit_trap
trap 'gfrm -rf $gftmp; rm -f $localtmp; exit $exit_code' 0 $trap_sigs

if
   # disable automatic replication
   gfmkdir $gftmp &&
   gfncopy -s 1 $gftmp &&

   # gfs_pio_write
   $gfs_pio_test $* -ctw -O $gftmpfile <$data/65byte &&
   $regress/bin/is_cksum_same $gftmpfile $data/65byte &&
   gfrm -f $gftmpfile &&

   # gfs_pio_write, no calculation
   $gfs_pio_test $* -ctw -S 1 -O $gftmpfile <$data/65byte &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   # gfs_pio_sendfile
   $gfs_pio_test $* -ctw -Y 0,0,-1 $gftmpfile <$data/65byte &&
   $regress/bin/is_cksum_same $gftmpfile $data/65byte &&
   gfrm -f $gftmpfile &&

   # gfs_pio_sendfile, no calculation
   $gfs_pio_test $* -ctw -Y 1,0,-1 $gftmpfile <$data/65byte &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   # gfs_pio_truncate to 0
   $gfs_pio_test $* -ctw -O -T 0 $gftmpfile <$data/65byte &&
   $regress/bin/is_cksum_same $gftmpfile /dev/null &&
   gfrm -f $gftmpfile &&

   # gfs_pio_truncate, rewind and gfs_pio_write
   cat $data/65byte $data/65byte |
   $gfs_pio_test $* -ctw -W 65 -T 0 -S 0 -O $gftmpfile &&
   $regress/bin/is_cksum_same $gftmpfile $data/65byte &&
   gfrm -f $gftmpfile &&

   # gfs_pio_truncate, and gfs_pio_sendfile
   cat $data/65byte $data/65byte |
   $gfs_pio_test $* -ctw -W 65 -T 0 -Y 0,0,-1 $gftmpfile &&
   $regress/bin/is_cksum_same $gftmpfile $data/65byte &&
   gfrm -f $gftmpfile &&

   # gfs_pio_truncate to shorter size, no calculation
   $gfs_pio_test $* -ctw -O -T 64 $gftmpfile <$data/65byte &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   # gfs_pio_truncate to just same size
   $gfs_pio_test $* -ctw -O -T 65 $gftmpfile <$data/65byte &&
   $regress/bin/is_cksum_same $gftmpfile $data/65byte &&
   gfrm -f $gftmpfile &&

   # gfs_pio_truncate to longer size, no calculation
   $gfs_pio_test $* -ctw -O -T 66 $gftmpfile <$data/65byte &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   # gfs_pio_truncate to longer size
   ( cat $data/65byte; awk 'BEGIN {printf "%c", 0; exit}' ) >$localtmp &&
   $gfs_pio_test $* -ct -O -T 66 -S 65 -I $gftmpfile <$data/65byte >/dev/null &&
   $regress/bin/is_cksum_same $gftmpfile $localtmp &&
   rm -f $localtmp &&
   gfrm -f $gftmpfile &&

   # gfs_pio_write/append (may be taken as writing whole while), no calculation
   echo ================================================================ |
   gfreg $* - $gftmpfile &&
   $gfs_pio_test $* -a -O $gftmpfile <$data/65byte &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   # gfs_pio_sendfile/append
   # (may be taken as writing whole while), no calculation
   echo ================================================================ |
   gfreg $* - $gftmpfile &&
   $gfs_pio_test $* -a -Y0,0,65 $gftmpfile <$data/65byte &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   true

then
    exit_code=$exit_pass
else
    exit_code=$exit_fail
fi
