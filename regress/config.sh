#!/bin/sh

PROGNAME=`basename $0`
remove_environment=true
hostname=
REGRESS_AUTH=sharedsecret
agent_enable=false
agent_only=false
: ${REGRESS_AGENT_PORT:=30603}
: ${REGRESS_BACKEND_PORT:=30602}
: ${REGRESS_GFMD_PORT:=30601}
: ${REGRESS_GFSD_PORT:=30600}
schema_version=
wait_seconds=3

usage()
{
	echo >&2 "Usage: $PROGNAME [<options>] [<regression_test>]"
	echo >&2 "mandatory options:"
	echo >&2 "	--prefix <installation_prefix>"
	echo >&2 "	--config-prefix <config_prefix>"
	echo >&2 "	-b <backend_type>"
	echo >&2 "options:"
	echo >&2 "	--help				: print this message"
	echo >&2 "	-k				: keep configuration"
	echo >&2 "	-h <hostname>"
	echo >&2 "	-a <auth_type>"
	echo >&2 "	-p <metadata_backend_port>"
	echo >&2 "	-m <gfmd_port>"
	echo >&2 "	-s <gfsd_port>"
	echo >&2 "	--agent-port <agent_port>"
	echo >&2 "	--agent-enable			: use gfarm_agent"
	echo >&2 "	--agent-only : disallow access to metadata server from clients"
	echo >&2 "	-S <schema_version>"
	exit 2
}

ABORT()
{
	[ $# -gt 0 ] && echo >&2 "${PROGNAME}: $@"
	echo >&2 "$PROGNAME failure"
	exit 1
}

while	case $1 in
	--prefix)
		prefix=${2?"$PROGNAME: --prefix option requires <installation_prefix> argument"}
		shift; true;;
	--config-prefix)
		CONFIG_PREFIX=${2?"$PROGNAME: --config-prefix option requires <config_prefix> argument"}
		shift; true;;
	-b)	REGRESS_BACKEND=${2?"$PROGNAME: -b option requires <backend> argument"}
		shift; true;;
	--help)	usage;;
	-k)	remove_environment=false; true;;
	-h)	BACKEND_HOSTNAME=${2?"$PROGNAME: -h option requires <hostname> argument"}
		hostname="-h $BACKEND_HOSTNAME"
		shift; true;;
	-a)	REGRESS_AUTH=${2?"$PROGNAME: -a option requires <auth_type> argument"}
		shift; true;;
	-p)	REGRESS_BACKEND_PORT=${2?"$PROGNAME: -p option requires <metadata_backend_port> argument"}
		shift; true;;
	-m)	REGRESS_GFMD_PORT=${2?"$PROGNAME: -m option requires <gfmd_port> argument"}
		shift; true;;
	-s)	REGRESS_GFSD_PORT=${2?"$PROGNAME: -s option requires <gfsd_port> argument"}
		shift; true;;
	--agent-port)
		REGRESS_AGENT_PORT=${2?"$PROGNAME: --agent-port option requires <agent_port> argument"}
		shift; true;;
	--agent-disable) agent_enable=false; true;; # this is default
	--agent-enable)	 agent_enable=true; true;;
	--agent-only)	 agent_enable=true; agent_only=true; true;;
	-S)	schema_version="-S ${2?$PROGNAME: -S option requires <schema_version> argument}"
		shift; true;;
	-*)	echo >&2 "$PROGNAME: unknown option $1"
		usage;;
	*)	false;;
	esac
do
	shift
done

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
if [ -z "$REGRESS_BACKEND" ]; then
	echo >&2 $PROGNAME: "-b <backend_type> option is necessary, aborted"
	exit 1
fi

case $# in
0)	set ./check.sh --prefix "$prefix";;
esac

PATH="$prefix/bin:$PATH:/bin:/sbin:/usr/bin:/usr/sbin:/usr/ucb:/usr/pkg/bin:/usr/pkg/sbin:/usr/local/bin:/usr/local/sbin"
export PATH
awk=awk
if [ -f /usr/bin/nawk ]; then awk=/usr/bin/nawk; fi

config_dir="${prefix}/share/gfarm/config"
. $config_dir/config-gfarm.$REGRESS_BACKEND
. $config_dir/config-gfarm.sysdep
set_first_defaults_$REGRESS_BACKEND

# sysdep_defaults must set: $RC_DIR
sysdep_defaults

: ${FQ_HOSTNAME:=`fq_hostname`}
: ${BACKEND_HOSTNAME:="$FQ_HOSTNAME"}

set_last_defaults_$REGRESS_BACKEND

config-gfarm --prefix $CONFIG_PREFIX -b $REGRESS_BACKEND $hostname \
	-a $REGRESS_AUTH -p $REGRESS_BACKEND_PORT \
	-m $REGRESS_GFMD_PORT -s $REGRESS_GFSD_PORT \
	$schema_version
sleep $wait_seconds
case $REGRESS_AUTH in
gsi|gsi_auth)
	if [ ! -w / ]; then # if i don't have root privilege?
		(
			echo ""
			echo "# cannot read host certificate"
			echo "spool_server_cred_type self"
			echo "metadb_server_cred_type self"
		) >>	$CONFIG_PREFIX/etc/gfarm.conf
		service_ctl gfmd restart
	fi;;
esac
if $agent_enable; then
config-agent --prefix $CONFIG_PREFIX -p $REGRESS_AGENT_PORT
	if $agent_only; then
		mv	  $CONFIG_PREFIX/etc/gfarm.conf \
			  $CONFIG_PREFIX/etc/gfarm.conf.org
		sed -e '/^ldap_/d' -e '/^postgresql_/d' \
			  $CONFIG_PREFIX/etc/gfarm.conf.org \
			> $CONFIG_PREFIX/etc/gfarm.conf
		REGRESS_AGENT=only
	else
		REGRESS_AGENT=enable
	fi
else
	REGRESS_AGENT=disable
fi
config-gfsd --prefix $CONFIG_PREFIX -l `hostname` $hostname
config-gfsd --prefix $CONFIG_PREFIX -l localhost -h localhost

GFARM_CONFIG_FILE=$CONFIG_PREFIX/etc/gfarm.conf
export GFARM_CONFIG_FILE
export REGRESS_BACKEND REGRESS_AUTH REGRESS_AGENT

gfmkdir '~'

"$@"

service_ctl gfsd-localhost stop
service_ctl gfsd-`hostname` stop
if $agent_enable; then
service_ctl gfarm_agent stop
fi
service_ctl gfmd stop
service_stop_$REGRESS_BACKEND
if $remove_environment; then
	rm -rf $CONFIG_PREFIX
fi
