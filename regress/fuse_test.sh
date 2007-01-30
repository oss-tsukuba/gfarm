#!/bin/sh

. ./regress.conf

gfarmfs_opt=
fuse_opt=
while case $1 in
	-O)	gfarmfs_opt=${2?"missing <gfarmfs_opt> argument for -O"}
		shift; true;;
	-o)	fuse_opt=${2?"missing <fuse_opt> argument for -o"}
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
		"<test_command>..."
	exit 1;;
esac

# NOTE: this redirects stdout to $log
. $regress/account.sh

fuse_mount_point=$localtop/fuse_mount_point.$$

fmt_init
print_both "Testing gfarmfs $gfarmfs_opt $fuse_mount_point $fuse_opt"

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
    tst="gfarmfs $@"
    eval_result $exit_fail
fi 
