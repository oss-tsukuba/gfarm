#!/bin/sh

PROGNAME=`basename $0`

if [ -z "$prefix" ]; then
	echo $PROGNAME: '${prefix} is not set' >&2
	exit 1
fi
config_dir="${prefix}/share/gfarm/config"

. $config_dir/config-gfarm.sysdep

case $1 in
-b)	BACKEND_TYPE=${2?"$PROGNAME: -b option requires <backend> argument"}
	shift 2;;
*)	BACKEND_TYPE=postgresql;;
esac
case $# in
1)	regress=./regress.sh;;
2)	regress=$2;;
*)	echo "Usage: $PROGNAME <config_prefix> [<regression_test>]" >&2
	exit 2;;
esac
CONFIG_PREFIX=$1

if [ -d "$CONFIG_PREFIX" ] || [ -f "$CONFIG_PREFIX" ]; then
	echo "$PROGNAME: $CONFIG_PREFIX already exists, aborted" >&2
	exit 1
fi

. $prefix/share/gfarm/config/config-gfarm.$BACKEND_TYPE
set_first_defaults_$BACKEND_TYPE

# sysdep_defaults must set: $RC_DIR
sysdep_defaults

PATH=$prefix/bin:$PATH 
export PATH

BACKEND_PORT=50602
GFMD_PORT=50601
GFSD_PORT=50600

set_last_defaults_$BACKEND_TYPE

config-gfarm --prefix $CONFIG_PREFIX -b $BACKEND_TYPE \
	-p $BACKEND_PORT -m $GFMD_PORT -s $GFSD_PORT 
config-gfsd --prefix $CONFIG_PREFIX -l `hostname`
config-gfsd --prefix $CONFIG_PREFIX -l localhost -h localhost

GFARM_CONFIG_FILE=$CONFIG_PREFIX/etc/gfarm.conf
export GFARM_CONFIG_FILE

gfmkdir '~'

case `uname` in
Linux)	env LD_PRELOAD="$prefix/lib/libgfs_hook.so.0 /usr/lib/gfarm/librt-not-hidden.so /usr/lib/gfarm/libpthread-not-hidden.so /usr/lib/gfarm/libc-not-hidden.so" $regress;;
SunOS)	env LD_PRELOAD_32=$prefix/lib/libgfs_hook.so.0 $regress;;
HP-UX)	env LD_PRELOAD=$prefix/lib/libgfs_hook.sl $regress;;
OSF1)	env _RLD_LIST="$prefix/lib/libgfs_hook.so.0:DEFAULT" $regress;;
Darwin)	env DYLD_INSERT_LIBRARIES=$prefix/lib/libgfs_hook.dylib DYLD_FORCE_FLAT_NAMESPACE= $regress;;
NetBSD|FreeBSD)
	env LD_PRELOAD=$prefix/lib/libgfs_hook.so.0 $regress;;
*)	echo "WARNING: unknown OS type `uname`. gfs_hook test will fail." >&2
	env LD_PRELOAD=$prefix/lib/libgfs_hook.so.0 $regress;;
esac


service_ctl gfsd-localhost stop
service_ctl gfsd-`hostname` stop
service_ctl gfmd stop
service_stop_$BACKEND_TYPE
rm -rf $CONFIG_PREFIX
