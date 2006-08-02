#!/bin/sh

PROGNAME=`basename $0`

usage()
{
	echo >&2 "Usage: $PROGNAME [<options>] [<regression_test>]"
	echo >&2 "mandatory options:"
	echo >&2 "	--prefix <installation_prefix>"
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
0)	usage;;
esac

: ${prefix?"$PROGNAME: --prefix <installation_prefix> option is mandatory"}

case `uname` in
Linux)	env LD_PRELOAD="$prefix/lib/libgfs_hook.so.0 /usr/lib/gfarm/librt-not-hidden.so /usr/lib/gfarm/libpthread-not-hidden.so /usr/lib/gfarm/libc-not-hidden.so" "$@";;
SunOS)	env LD_PRELOAD_32=$prefix/lib/libgfs_hook.so.0:/usr/lib/libresolv.so "$@";;
HP-UX)	env LD_PRELOAD=$prefix/lib/libgfs_hook.sl "$@";;
OSF1)	env _RLD_LIST="$prefix/lib/libgfs_hook.so.0:DEFAULT" "$@";;
Darwin)	env DYLD_INSERT_LIBRARIES=$prefix/lib/libgfs_hook.dylib DYLD_FORCE_FLAT_NAMESPACE= "$@";;
NetBSD|FreeBSD)
	env LD_PRELOAD=$prefix/lib/libgfs_hook.so.0 "$@";;
*)	echo >&2 "WARNING: unknown OS type `uname`. gfs_hook test may fail."
	env LD_PRELOAD=$prefix/lib/libgfs_hook.so.0 "$@";;
esac
