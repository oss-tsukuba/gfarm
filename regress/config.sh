#!/bin/sh

PROGNAME=`basename $0`
remove_environment=true
hostname=
BACKEND_PORT=50602
GFMD_PORT=50601
GFSD_PORT=50600

usage()
{
	echo >&2 "Usage: $PROGNAME [<options>] [<regression_test>]"
	echo >&2 "mandatory options:"
	echo >&2 "	--prefix <installatioin_prefix>"
	echo >&2 "	--config-prefix <config_prefix>"
	echo >&2 "	-b <backend_type>"
	echo >&2 "options:"
	echo >&2 "	--help				: print this message"
	echo >&2 "	-k				: keep configuration"
	echo >&2 "	-h <hostname>"
	echo >&2 "	-p <metadata_backend_port>"
	echo >&2 "	-m <gfmd_port>"
	echo >&2 "	-s <gfsd_port>"
	exit 2
}

while	case $1 in
	--prefix)
		prefix=${2?"$PROGNAME: --prefix option requires <installation_prefix> argument"}
		shift; true;;
	--config-prefix)
		CONFIG_PREFIX=${2?"$PROGNAME: --config-prefix option requires <config_prefix> argument"}
		shift; true;;
	-b)	BACKEND_TYPE=${2?"$PROGNAME: -b option requires <backend> argument"}
		shift; true;;
	--help)	usage;;
	-k)	remove_environment=false; true;;
	-h)	BACKEND_HOSTNAME=${2?"$PROGNAME: -h option requires <hostname> argument"}
		hostname="-h $BACKEND_HOSTNAME"
		shift; true;;
	-p)	BACKEND_PORT=${2?"$PROGNAME: -p option requires <metadata_backend_port> argument"}
		shift; true;;
	-m)	GFMD_PORT=${2?"$PROGNAME: -m option requires <gfmd_port> argument"}
		shift; true;;
	-s)	GFSD_PORT=${2?"$PROGNAME: -s option requires <gfsd_port> argument"}
		shift; true;;
	-*)	echo >&2 "$PROGNAME: unknown option $1"
		usage;;
	*)	false;;
	esac
do
	shift
done

case $# in
0)	set ./regress.sh;;
esac

if [ -z "$prefix" ]; then
	echo >&2 $PROGNAME: '"--prefix <installation_prefix>" option is not set, aborted'
	exit 1
elif [ ! -d "$prefix" ]; then
	echo >&2 $PROGNAME: "$prefix is not a directory, aborted"
	exit 1
fi
if [ -z "$CONFIG_PREFIX" ]; then
	echo >&2 $PROGNAME: '"--config-prefix <config_prefix>" option is necessary, aborted'
	exit 1
fi
if [ -d "$CONFIG_PREFIX" ] || [ -f "$CONFIG_PREFIX" ]; then
	echo >&2 $PROGNAME: "$CONFIG_PREFIX already exists, aborted"
	exit 1
fi
if [ -z "$BACKEND_TYPE" ]; then
	echo >&2 $PROGNAME: "-b <backend_type> option is necessary, aborted"
	exit 1
fi

PATH="$prefix/bin:$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/ucb:/usr/pkg/bin:/usr/pkg/sbin:/usr/local/bin:/usr/local/sbin"
export PATH
awk=awk
if [ -f /usr/bin/nawk ]; then awk=/usr/bin/nawk; fi

config_dir="${prefix}/share/gfarm/config"
. $config_dir/config-gfarm.$BACKEND_TYPE
. $config_dir/config-gfarm.sysdep
set_first_defaults_$BACKEND_TYPE

# sysdep_defaults must set: $RC_DIR
sysdep_defaults

: ${FQ_HOSTNAME:=`fq_hostname`}
: ${BACKEND_HOSTNAME:="$FQ_HOSTNAME"}

set_last_defaults_$BACKEND_TYPE

config-gfarm --prefix $CONFIG_PREFIX -b $BACKEND_TYPE $hostname \
	-p $BACKEND_PORT -m $GFMD_PORT -s $GFSD_PORT 
config-gfsd --prefix $CONFIG_PREFIX -l `hostname` $hostname
config-gfsd --prefix $CONFIG_PREFIX -l localhost -h localhost

GFARM_CONFIG_FILE=$CONFIG_PREFIX/etc/gfarm.conf
export GFARM_CONFIG_FILE

gfmkdir '~'

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


service_ctl gfsd-localhost stop
service_ctl gfsd-`hostname` stop
service_ctl gfmd stop
service_stop_$BACKEND_TYPE
if $remove_environment; then
	rm -rf $CONFIG_PREFIX
fi

