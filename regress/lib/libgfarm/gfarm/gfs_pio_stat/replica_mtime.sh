#!/bin/sh

# test SF.net #959 - gfs_pio_stat() may return meaningless st_mtime/st_atime,
# if it's called against a replica

. ./regress.conf
exit_code=$exit_xfail
gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
GFPREP=$regress/bin/gfprep_for_test

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if
   # set ncopy=1 before any replication
   $gfs_pio_test -ct $gftmp &&
   gfncopy -s 1 $gftmp &&

   gfreg $data/1byte $gftmp &&

   # make the problem apparent even on a filesystem which tv_nsec is always 0
   sleep 1 &&

   gfncopy -s 2 $gftmp &&
   $GFPREP -N 2 gfarm://${gftmp} &&
   gfstat $gftmp >$localtmp &&

   host1=`gfwhere $gftmp | awk '{print $1}'` &&
   host2=`gfwhere $gftmp | awk '{print $1}'` &&
   gfstat -h $host1 -r $gftmp | diff -u - $localtmp &&
   gfstat -h $host2 -r $gftmp | diff -u - $localtmp
then
  exit_code=$exit_xpass
fi

rm -f $localtmp
gfrm -f $gftmp
exit $exit_code
