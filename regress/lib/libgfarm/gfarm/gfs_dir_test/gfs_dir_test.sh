#!/bin/sh

. ./regress.conf

gfs_dir_test=$testbin/gfs_dir_test

cleanup()
{
   for i in aaa bbb ccc ddd eee; do
      gfrm -f $gftmp/$i
   done
   gfrmdir $gftmp
}

trap 'cleanup; exit $exit_trap' $trap_sigs

cmp_entry()
{
   opt=$1
   read_skip=$2

   entry=`$gfs_dir_test $opt $read_skip -R $gftmp | tail -1`
   position=`$gfs_dir_test $opt -s $read_skip -T $gftmp`
   [ X"`$gfs_dir_test $opt -S $position -R $gftmp`" = X"$entry" ] &&
   [ X"`$gfs_dir_test $opt $read_skip -R -R -S $position -R $gftmp | tail -1`" = X"$entry" ]
}

run_test()
{
   opt=$1

   if $gfs_dir_test $opt -A $gftmp | sort | cmp - $testbase/all.out; then
      :
   else
      return 1
   fi

   cmp_entry "$opt" "" &&
   cmp_entry "$opt" "-R -R -R -R" &&
   cmp_entry "$opt" "-R -R -R -R -R -R -R -R -R -R"
}

if gfmkdir $gftmp &&
   gfreg $data/0byte $gftmp/aaa &&
   gfreg $data/0byte $gftmp/bbb &&
   gfreg $data/0byte $gftmp/ccc &&
   gfreg $data/0byte $gftmp/ddd &&
   gfreg $data/0byte $gftmp/eee &&
   run_test '' &&
   run_test -e
then
   exit_code=$exit_pass
fi

cleanup
exit $exit_code
