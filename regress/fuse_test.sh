#!/bin/sh

. ./regress.conf
. $regress/account.sh

gfarmfs_opt=
fuse_opt=
fuse_mount_point=$localtop/fuse_mount_point.$$
while case $1 in
	-O)	gfarmfs_opt=${2?"missing <gfarmfs_option> argument for -O"}
		shift; true;;
	-o)	fuse_opt=${2?"missing <fuse_option> argument for -o"}
		shift; true;;
	-m)	fuse_mount_point=${2?"missing <fuse_mount_point> argument for -m"}
		shift; true;;
	*)	false;;
	esac
do
	shift
done
case $# in
0)	echo >&2 "Usage: fuse_test.sh " \
		"[-O <gfarmfs_option>] " \
		"[-o <fuse_option>] " \
		"[-m <fuse_mount_point>] " \
		"<test_command>..."
	exit 1;;
esac

print_both "Testing gfarmfs $gfarmfs_opt $fuse_mount_point $fuse_opt" >>$log

failed=true

if mkdir $fuse_mount_point
then
    if gfarmfs $gfarmfs_opt $fuse_mount_point $fuse_opt
    then 
	REGRESS_HOOK_MODE=gfarmfs \
		REGRESS_GFARMFS_OPT="$gfarmfs_opt" \
		REGRESS_FUSE_OPT="$fuse_opt" \
		REGRESS_HOOKTOP="$fuse_mount_point/`gfwhoami`" "$@"
	if fusermount -u $fuse_mount_point
	then
	    failed=false;
	fi
    fi
    rmdir $fuse_mount_point
fi
if $failed
then
    tst="gfarmfs $gfarmfs_opt $fuse_mount_point $fuse_opt: $@"
    print_result $exit_fail >>$log
fi 
