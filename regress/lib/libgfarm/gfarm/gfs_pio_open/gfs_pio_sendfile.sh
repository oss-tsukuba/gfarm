#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

trap 'gfrm -f $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if 
   # input from a file
   $gfs_pio_test -ctw -Y 0,0,-1 $* $gftmp <$data/65byte &&
	gfstat $gftmp | awk '$1 == "Size:" {exit($2 == 65 ? 0 : 1)}' &&
	gfexport $gftmp | cmp - $data/65byte &&

   # input from a pipe
   cat $data/65byte | $gfs_pio_test -ctw -Y 0,0,-1 $* $gftmp &&
	gfstat $gftmp | awk '$1 == "Size:" {exit($2 == 65 ? 0 : 1)}' &&
	gfexport $gftmp | cmp - $data/65byte &&

   # input from a file, r_off != 0
   $gfs_pio_test -ctw -Y 0,11,1 $* $gftmp <$data/65byte &&
	gfstat $gftmp | awk '$1 == "Size:" {exit($2 == 1 ? 0 : 1)}' &&
	gfexport $gftmp | cmp - $data/1byte &&

   # input from a pipe, r_off != 0: should fail with "illegal seek"
   cat $data/65byte | $gfs_pio_test -ctw -Y 0,11,1 $* $gftmp 2>&1 |
	grep ': illegal seek$' >/dev/null &&

   # input from a file, w_off != 0
   echo "0123456789" | gfreg - $gftmp &&
   $gfs_pio_test -w -Y 5,61,-1 $* $gftmp <$data/65byte &&
	gfstat $gftmp | awk '$1 == "Size:" {exit($2 == 11 ? 0 : 1)}' &&
	gfexport $gftmp >$localtmp &&
	( echo "01234xyz"; echo "9" ) | cmp - $localtmp &&

   # input from a pipe, w_off != 0
   echo "0123456789" | gfreg - $gftmp &&
   echo "xyz" | $gfs_pio_test -w -Y 5,0,-1 $* $gftmp &&
	gfstat $gftmp | awk '$1 == "Size:" {exit($2 == 11 ? 0 : 1)}' &&
	gfexport $gftmp >$localtmp &&
	( echo "01234xyz"; echo "9" ) | cmp - $localtmp &&

   # input from a file, w_off != 0, filesize is changed
   echo "0123456789" | gfreg - $gftmp &&
   $gfs_pio_test -w -Y 9,34,4 $* $gftmp <$data/65byte &&
	gfstat $gftmp | awk '$1 == "Size:" {exit($2 == 13 ? 0 : 1)}' &&
	gfexport $gftmp >$localtmp &&
	( echo "012345678XYZ" ) | cmp - $localtmp &&

   # input from a pipe, w_off != 0, filesize is changed
   echo "0123456789" | gfreg - $gftmp &&
   ( echo "XYZ"; echo "abc" ) | $gfs_pio_test -w -Y 9,0,4 $* $gftmp &&
	gfstat $gftmp | awk '$1 == "Size:" {exit($2 == 13 ? 0 : 1)}' &&
	gfexport $gftmp >$localtmp &&
	( echo "012345678XYZ" ) | cmp - $localtmp

then
    exit_code=$exit_pass
fi

gfrm $gftmp
rm -f $localtmp
exit $exit_code
