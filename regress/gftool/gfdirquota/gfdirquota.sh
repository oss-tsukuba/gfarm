#!/bin/sh

. ./regress.conf

dirset=`hostname`.regress.set-$$
gfls_D="`gfwhoami`":${dirset}
basename=`basename $gftmp`

trap 'gfrm -rf $gftmp; gfdirquota -d $dirset; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   gfdirquota -c $dirset &&
   gfdirquota | grep "^${dirset}\$" >/dev/null &&
   gfdirquota -l | grep "^${gfls_D}:\$" >/dev/null &&
   gfdirquota -a $dirset $gftmp &&
   gfdirquota -l $dirset | sed -n '$p' | grep "^${gftmp}\$" >/dev/null &&
   gfquota -D $dirset | sed 1,2d | cmp -s - $testbase/gfedquota.out.0 &&
   gfquota -d $gftmp  | sed 1,3d | cmp -s - $testbase/gfedquota.out.0 &&

   # gfls -D
   [ `gfls -1Dd $gftop | wc -l` -eq 1 ] &&
   [ `gfls -1Dd $gftmp/.. | wc -l` -eq 1 ] &&
   gfls -1Dd $gftmp |
	awk '{ if ($1 == "'"${gfls_D}"'") exit 0; else exit 1; }' &&
   gfls -1D $gftmp/.. | awk '
	BEGIN {status=1}
	$NF == "'"$basename"'" {
	if (NF == 2 && $1 == "'"$gfls_D"'") { status=0; exit 0} else exit 1}
	END { exit status }' &&

   # only empty dirset can be deleted
   gfdirquota -d $dirset 2>&1 | grep 'directory quota exists' >/dev/null &&

   gfrmdir $gftmp &&
   ( gfdirquota -l $dirset | sed -n '$p' | grep "^${gftmp}\$" >/dev/null;
     [ $? -ne 0 ] ) &&
   gfdirquota -d $dirset
then
	exit_code=$exit_pass
fi

gfrm -rf $gftmp
gfdirquota -d $dirset 2>/dev/null
exit $exit_code
