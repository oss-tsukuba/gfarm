#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
bufsize=`gfstatus client_file_bufsize`

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if
   awk 'BEGIN{
      for (i = 0; i < '$bufsize' * 2; i++) printf "a";
      exit
   }' > $localtmp &&

   # gfs_pio_read, then gfs_pio_write
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a"
      for (i = 0; i < '$bufsize'; i++) printf "%c", 0
      exit
   }' | gfreg $* - $gftmp &&
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a";
      exit
   }' |
   $gfs_pio_test $* -R $bufsize -S $bufsize -O $gftmp >/dev/null &&
   $regress/bin/is_cksum_same $gftmp $localtmp &&
   gfrm -f $gftmp &&

   # gfs_pio_write, then gfs_pio_read
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "%c", 0
      for (i = 0; i < '$bufsize'; i++) printf "a"
      exit
   }' | gfreg $* - $gftmp &&
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a";
      exit
   }' |
   $gfs_pio_test $* -S 0 -W $bufsize -S $bufsize -I $gftmp >/dev/null &&
   $regress/bin/is_cksum_same $gftmp $localtmp &&
   gfrm -f $gftmp &&

   # gfs_pio_write/append, then gfs_pio_read
   # (may be taken as accessing whole while), no calculation
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "%c", 0
      exit
   }' | gfreg $* - $gftmp &&
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a";
      exit
   }' |
   $gfs_pio_test $* -a -S 0 -W $bufsize -S $bufsize -I $gftmp >/dev/null &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfrm -f $gftmp &&

   # gfs_pio_sendfile/append, then gfs_pio_read
   # (may be taken as accessing whole while), no calculation
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "%c", 0
      exit
   }' | gfreg $* - $gftmp &&
   awk 'BEGIN{
      for (i = 0; i < '$bufsize'; i++) printf "a";
      exit
   }' |
   $gfs_pio_test $* -a -Y 0,0,$bufsize -S $bufsize -I $gftmp >/dev/null &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfrm -f $gftmp &&

   true

then
    exit_code=$exit_pass
fi

gfrm -f $gftmp
rm -f $localtmp
exit $exit_code
