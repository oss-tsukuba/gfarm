#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test

# client does not run on the filesystem node

if host=`$regress/bin/get_remote_gfhost` &&
   $regress/bin/is_digest_enabled
then
	$testbase/cksum_basic.sh -h $host
	exit_code=$?
	if [ $exit_code != $exit_pass ]; then
		exit $exit_code
	fi

	# cksum will be set by reading remote file,
	# (but not by reading local file)

	trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

	# creating a file without cksum
	cat $data/65byte $data/65byte |
		$gfs_pio_test -h $host -ct -O -T 65 $gftmp &&
	[ X"`gfcksum -t $gftmp`" = X"" ] &&
	# reading the file by gfs_pio_read
	$gfs_pio_test -h $host -r -I $gftmp >/dev/null &&
	$regress/bin/is_cksum_same $gftmp $data/65byte &&
	gfrm -f $gftmp &&

	# creating a file without cksum
	cat $data/65byte $data/65byte |
		$gfs_pio_test -h $host -ct -O -T 65 $gftmp &&
	[ X"`gfcksum -t $gftmp`" = X"" ] &&
	# reading the file by gfs_pio_recvfile
	$gfs_pio_test -h $host -r -A 0,0,-1 $gftmp >/dev/null &&
	$regress/bin/is_cksum_same $gftmp $data/65byte &&
	gfrm -f $gftmp &&

	true

else
	exit $exit_unsupported
fi
