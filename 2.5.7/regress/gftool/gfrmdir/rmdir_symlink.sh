#!/bin/sh

. ./regress.conf

trap 'gfrm -rf $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   gfmkdir $gftmp/dir &&
   gfln -s $gftmp/dir $gftmp/symlink
then
   if gfrmdir $gftmp/symlink; then
	:
   else
	exit_code=$exit_pass
   fi
fi

gfrm -rf $gftmp
exit $exit_code
