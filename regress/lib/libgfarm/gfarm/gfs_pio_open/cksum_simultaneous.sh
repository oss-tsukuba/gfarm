#!/bin/sh

. ./regress.conf

SLEEP_TIME=3
gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
gftmpfile=$gftmp/file
localtmp2=${localtmp}2

exit_code=$exit_trap
trap 'gfrm -rf $gftmp; rm -f $localtmp $localtmp2; exit $exit_code' \
	0 $trap_sigs

if
   # disable automatic replication
   gfmkdir $gftmp &&
   gfncopy -s 1 $gftmp &&

   # simultaneous write, no calculation
   gfreg $* $data/65byte $gftmpfile &&
   (
     $gfs_pio_test $* -w -P $SLEEP_TIME -O -P$SLEEP_TIME $gftmpfile \
	<$data/65byte &
     $gfs_pio_test $* -w -O -P$SLEEP_TIME -P$SLEEP_TIME $gftmpfile \
	<$data/65byte &
     wait
   ) &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfrm -f $gftmpfile &&

   # simultaneous write & read -> DOES NOT DETECT
   gfreg $* $data/65byte $gftmpfile &&
   (
     $gfs_pio_test $* -w -P $SLEEP_TIME -S 65 -O $gftmpfile <$data/65byte &
     ( $gfs_pio_test $* -r -R 65 -P$SLEEP_TIME -P $SLEEP_TIME -I $gftmpfile \
	<$data/65byte >$localtmp
       echo $? >$localtmp2
     ) &
     wait
   ) &&
   [ X"`cat $localtmp2`" = X"0" ] &&
   ( cat $data/65byte $data/65byte ) | cmp -s - $localtmp &&
   gfrm -f $gftmpfile &&
   rm -f $localtmp $localtmp2 &&

   true
then
    exit_code=$exit_pass
else
    exit_code=$exit_fail
fi
