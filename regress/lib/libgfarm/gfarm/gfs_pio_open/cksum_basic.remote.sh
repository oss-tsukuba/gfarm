#!/bin/sh

. ./regress.conf

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
gftmpfile=$gftmp/file

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

    exit_code=$exit_trap
    trap 'gfrm -rf $gftmp; exit $exit_code' 0 $trap_sigs

    if
	# disable automatic replication
	gfmkdir -p $gftmp &&
	gfncopy -s 1 $gftmp &&

	# creating a file without cksum
	cat $data/65byte $data/65byte |
		$gfs_pio_test -h $host -ct -O -T 65 $gftmpfile &&
	[ X"`gfcksum -t $gftmpfile`" = X"" ] &&
	# reading the file by gfs_pio_read
	$gfs_pio_test -h $host -r -I $gftmpfile >/dev/null &&
	$regress/bin/is_cksum_same $gftmpfile $data/65byte &&
	gfrm -f $gftmpfile &&

	# creating a file without cksum
	cat $data/65byte $data/65byte |
		$gfs_pio_test -h $host -ct -O -T 65 $gftmpfile &&
	[ X"`gfcksum -t $gftmpfile`" = X"" ] &&
	# reading the file by gfs_pio_recvfile
	$gfs_pio_test -h $host -r -A 0,0,-1 $gftmpfile >/dev/null &&
	$regress/bin/is_cksum_same $gftmpfile $data/65byte &&
	gfrm -f $gftmpfile &&

	true
    then
	exit_code=$exit_pass
    else
	exit_code=$exit_fail
    fi
else
    exit $exit_unsupported
fi
