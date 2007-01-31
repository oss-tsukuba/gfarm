#!/bin/sh

. ./regress.conf
. $regress/account.sh

$regress/regress.sh -t $regress/schedule

set $regress/regress.sh -t $regress/schedule.hook

print_both "Testing gfs_hook" >>$log
"$@"

if [ "${REGRESS_GFARMFS_FUSE+set}" = "set" ] &&
   [  -d "$REGRESS_GFARMFS_FUSE" ]
then
	$regress/fuse_test.sh "$@"
	$regress/fuse_test.sh -O "-nlsu" "$@"
	$regress/fuse_test.sh -O "-nlsu" -o "-o direct_io" "$@"
	$regress/fuse_test.sh -O "-nlsu" -o "-o default_permissions" "$@"
	$regress/fuse_test.sh -O "-nlsu" -o "-o attr_timeout=0" "$@"
	$regress/fuse_test.sh -O "-nlsu -N2" "$@"
	$regress/fuse_test.sh -O "-nlsu -b" "$@"
	$regress/fuse_test.sh -O "-nlsu -b" -o "-o direct_io" "$@"

#	$regress/fuse_test.sh -O "-nlsu -b" \
#		$regress/tst.sh gfs_hook/tool/postgresql/make-check.sh
fi
