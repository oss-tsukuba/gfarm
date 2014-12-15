#!/bin/sh

# NOTE:	"gfcksum -t", "gfcksum -T" and "gfcksum" are tested by
#	regress/lib/libgfarm/gfarm/gfs_pio_open/cksum_*.sh

. ./regress.conf

gfs_pio_test=$testbin/../../lib/libgfarm/gfarm/gfs_pio_test/gfs_pio_test

trap 'gfrm -rf $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

$regress/bin/is_digest_enabled || exit $exit_unsupported

if
   # metadata has checksum
   gfreg $data/65byte $gftmp &&
   gfcksum -c $gftmp >/dev/null &&
   gfrm -f $gftmp &&
  
   # metadata does not have checksum
   cat $data/65byte $data/65byte |
	$gfs_pio_test -ct -O -T 65 $gftmp &&
   [ X"`gfcksum -t $gftmp`" = X"" ] &&
   gfcksum -c $gftmp >$localtmp &&
   type=`awk '{print substr($2, 2, length($2)-2)}' $localtmp` &&
   cksum=`openssl "$type" $data/65byte | awk '{print $NF}'` &&
   [ X"$cksum" = X"`awk '{print $1}' $localtmp`" ] &&
   rm -f $localtmp &&
   gfrm -f $gftmp &&

   true

then
  exit $exit_pass
else
  exit $exit_fail
fi


  
