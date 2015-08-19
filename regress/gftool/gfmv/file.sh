#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp/1byte $gftmp/xxx $gftmp/yyy $gftmp/dir1/1byte $gftmp/dir1/xxx $gftmp/dir1/yyy $gftmp/dir2/1byte $gftmp/dir2/xxx $gftmp/dir2/yyy; gfrmdir $gftmp/dir1 $gftmp/dir2 $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&

   gfreg $data/1byte $gftmp/1byte &&
   gfmv $gftmp/1byte $gftmp/xxx &&
   gfexport $gftmp/xxx | cmp -s - $data/1byte &&

   gfreg $data/1byte $gftmp/1byte &&
   gfreg $data/1byte $gftmp/yyy &&
   if gfmv $gftmp/1byte $gftmp/xxx $gftmp/yyy; then false; else true; fi &&

   gfmkdir $gftmp/dir1 &&
   gfmv $gftmp/1byte $gftmp/xxx $gftmp/yyy $gftmp/dir1 &&
   gfls $gftmp/dir1/1byte &&
   gfls $gftmp/dir1/xxx &&
   gfls $gftmp/dir1/yyy &&

   gfmkdir $gftmp/dir2 &&
   gfreg $data/1byte $gftmp/dir2/1byte &&
   gfchmod 0500 $gftmp/dir2/1byte &&
   echo n | gfmv -i $gftmp/dir1/'*' $gftmp/dir2 &&
   gfls $gftmp/dir1/1byte &&
   gfls $gftmp/dir2/1byte &&
   gfls $gftmp/dir2/xxx &&
   gfls $gftmp/dir2/yyy
then
	exit_code=$exit_pass
fi

gfrm $gftmp/dir1/1byte $gftmp/dir2/1byte $gftmp/dir2/xxx $gftmp/dir2/yyy
gfrmdir $gftmp/dir1 $gftmp/dir2 $gftmp
exit $exit_code
