#!/bin/sh

. ./regress.conf

SLEEP_TIME=3
gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
localtmp2=${localtmp}2

trap 'gfrm -f $gftmp; rm -f $localtmp $localtmp2; exit $exit_trap' $trap_sigs

if
   # simultaneous write, no calculation
   gfreg $* $data/65byte $gftmp &&
   (
     $gfs_pio_test $* -w -P $SLEEP_TIME -O -P$SLEEP_TIME $gftmp <$data/65byte &
     $gfs_pio_test $* -w -O -P$SLEEP_TIME -P$SLEEP_TIME $gftmp <$data/65byte &
     wait
   ) &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfrm -f $gftmp &&

   # simultaneous write & read -> DOES NOT DETECT
   gfreg $* $data/65byte $gftmp &&
   (
     $gfs_pio_test $* -w -P $SLEEP_TIME -S 65 -O $gftmp <$data/65byte &
     ( $gfs_pio_test $* -r -R 65 -P$SLEEP_TIME -P $SLEEP_TIME -I $gftmp <$data/65byte >$localtmp
       echo $? >$localtmp2
     ) &
     wait
   ) &&
   [ X"`cat $localtmp2`" = X"0" ] &&
   ( cat $data/65byte $data/65byte ) | cmp -s - $localtmp &&
   gfrm -f $gftmp &&
   rm -f $localtmp $localtmp2 &&

   true
then
    exit_code=$exit_pass
fi

gfrm -f $gftmp
rm -f $localtmp $localtmp2
exit $exit_code
