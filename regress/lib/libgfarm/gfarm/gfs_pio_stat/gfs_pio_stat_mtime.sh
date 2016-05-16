#!/bin/sh

# test SF.net #958 - gfs_pio_stat() may change st_mtime/st_atime,
# if it's called against a never accessed 0-byte file

. ./regress.conf
exit_code=$exit_xfail
gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if
  $gfs_pio_test -ct $gftmp &&

  # make the problem apparent even on a filesystem which tv_nsec is always 0
  sleep 1 &&

  gfstat $gftmp | egrep '^(Modify|Access):' >$localtmp &&

  $gfs_pio_test -Q $gftmp &&
#"$gfs_pio_test -I $gftmp" changes Modify time, and that's another problem.

  gfstat $gftmp | egrep '^(Modify|Access):' | diff -u - $localtmp
then
  exit_code=$exit_xpass
fi

rm -f $localtmp
gfrm -f $gftmp
exit $exit_code
