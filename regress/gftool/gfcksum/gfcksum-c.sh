#!/bin/sh

# NOTE:	"gfcksum -t", "gfcksum -T" and "gfcksum" are tested by
#	regress/lib/libgfarm/gfarm/gfs_pio_open/cksum_*.sh

. ./regress.conf

$regress/bin/is_digest_enabled || exit $exit_unsupported

gfs_pio_test=$testbin/../../lib/libgfarm/gfarm/gfs_pio_test/gfs_pio_test
gftmpfile=$gftmp/file

trap 'gfrm -rf $gftmp; rm -f $localtmp; exit $exit_code' 0 $trap_sigs
exit_code=$exit_trap

if
   # disable automatic replication
   gfmkdir $gftmp &&
   gfncopy -s 1 $gftmp &&

   # metadata has checksum
   gfreg $data/65byte $gftmpfile &&
   gfcksum -c $gftmpfile >/dev/null &&
   gfrm -f $gftmpfile &&
  
   # metadata does not have checksum
   cat $data/65byte $data/65byte |
	$gfs_pio_test -ct -O -T 65 $gftmpfile &&
   [ X"`gfcksum -t $gftmpfile`" = X"" ] &&
   gfcksum -c $gftmpfile >$localtmp &&
   type=`awk '{print substr($2, 2, length($2)-2)}' $localtmp` &&
   cksum=`openssl "$type" $data/65byte | awk '{print $NF}'` &&
   [ X"$cksum" = X"`awk '{print $1}' $localtmp`" ]

then
  exit_code=$exit_pass
else
  exit_code=$exit_fail
fi
