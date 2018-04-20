#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
gftmpfile=$gftmp/file
bufsize=`gfstatus client_file_bufsize`

exit_code=$exit_trap
trap 'gfrm -rf $gftmp; rm -f $localtmp; exit $exit_code' 0 $trap_sigs

if
   # disable automatic replication
   gfmkdir $gftmp &&
   gfncopy -s 1 $gftmp &&

   awk 'BEGIN{
      for (i = 0; i < '$bufsize' * 2; i++) printf "a";
      exit
   }' > $localtmp &&

   # gfs_pio_read, then gfs_pio_write
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a"
      for (i = 0; i < '$bufsize'; i++) printf "%c", 0
      exit
   }' | gfreg $* - $gftmpfile &&
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a";
      exit
   }' |
   $gfs_pio_test $* -R $bufsize -S $bufsize -O $gftmpfile >/dev/null &&
   $regress/bin/is_cksum_same $gftmpfile $localtmp &&
   gfrm -f $gftmpfile &&

   # gfs_pio_write, then gfs_pio_read
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "%c", 0
      for (i = 0; i < '$bufsize'; i++) printf "a"
      exit
   }' | gfreg $* - $gftmpfile &&
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a";
      exit
   }' |
   $gfs_pio_test $* -S 0 -W $bufsize -S $bufsize -I $gftmpfile >/dev/null &&
   $regress/bin/is_cksum_same $gftmpfile $localtmp &&
   gfrm -f $gftmpfile &&

   # gfs_pio_write/append, then gfs_pio_read
   # (may be taken as accessing whole while), no calculation
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "%c", 0
      exit
   }' | gfreg $* - $gftmpfile &&
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a";
      exit
   }' |
   $gfs_pio_test $* -a -S 0 -W $bufsize -S $bufsize -I $gftmpfile >/dev/null &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   # gfs_pio_sendfile/append, then gfs_pio_read
   # (may be taken as accessing whole while), no calculation
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "%c", 0
      exit
   }' | gfreg $* - $gftmpfile &&
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a";
      exit
   }' |
   $gfs_pio_test $* -a -Y 0,0,$bufsize -S $bufsize -I $gftmpfile >/dev/null &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   true

then
    exit_code=$exit_pass
else
    exit_code=$exit_fail
fi
