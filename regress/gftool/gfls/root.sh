#!/bin/sh

. ./regress.conf

trap 'rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfls -d / >$localtmp &&
   cmp -s $localtmp $testbase/root.out &&

   gfls -ali / >$localtmp &&
   awk '$NF == "." { dot_inum = $1 }
	$NF == ".." { if ($1 == dot_inum) exit 0; else exit 1 }' $localtmp

then
	exit_code=$exit_pass
fi

rm -f $localtmp
exit $exit_code
