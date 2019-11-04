#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfreg $* $data/65byte $gftmp &&

   # output to a file
   $gfs_pio_test -r -A 0,0,-1 $* $gftmp >$localtmp &&
	cmp $localtmp $data/65byte &&

   # output to a pipe
   $gfs_pio_test -r -A 0,0,-1 $* $gftmp | cmp - $data/65byte &&

   # output to a file, r_off != 0
   $gfs_pio_test -r -A 11,0,1 $* $gftmp >$localtmp &&
	cmp $localtmp $data/1byte &&

   # output to a pipe, r_off != 0
   $gfs_pio_test -r -A 11,0,1 $* $gftmp | cmp - $data/1byte &&

   # output to a file, w_off != 0
   ( echo "0123456789";  $gfs_pio_test -r -A 61,5,-1 $* $gftmp ) >$localtmp &&
	( echo "01234xyz"; echo "9" ) | cmp - $localtmp &&

   # output to a pipe, w_off != 0: should fail with "illegal seek"
   ( $gfs_pio_test -r -A 61,5,-1 $* $gftmp | cat >dev/null ) 2>&1 |
	grep ': illegal seek$' >/dev/null
then
    exit_code=$exit_pass
fi

gfrm $gftmp
rm -f $localtmp
exit $exit_code
