#!/bin/sh

PROGNAME=`basename $0`

usage()
{
	echo >&2 "Usage: $PROGNAME --prefix <installation_prefix>"
	exit 2
}

while	case $1 in
	--prefix)
		prefix=${2?"$PROGNAME: --prefix option requires <installation_prefix> argument"}
		shift; true;;
	--help)	usage;;
	-*)	echo >&2 "$PROGNAME: unknown option $1"
		usage;;
	*)	false;;
	esac
do
	shift
done

case $# in
0)	:;;
*)	usage;;
esac

: ${prefix?"$PROGNAME: --prefix <installation_prefix> option is mandatory"}

. ./regress.conf
. $regress/account.sh

$regress/regress.sh -t $regress/schedule

set $regress/regress.sh -t $regress/schedule.hook

print_both "Testing gfs_hook" >>$log
$regress/gfs_hook.sh --prefix $prefix "$@"

# The follownig fsystest.sh really should belong to $regress/schedule.hook,
# but GfarmFS-FUSE already tested it against various gfarmfs options.
$regress/gfs_hook.sh --prefix $prefix \
	$regress/tst.sh gfs_hook/lib/fsystest/fsystest.sh


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

	if [ "${REGRESS_POSTGRESQL+set}" = "set" ]
	then
	    $regress/fuse_test.sh -O "-nlsu -b" -m $REGRESS_POSTGRESQL \
		$regress/tst.sh $regress/gfs_hook/tool/postgresql/make-check.sh
	fi	
fi
