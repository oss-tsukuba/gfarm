#!/bin/sh
#
# $FreeBSD$
#

# PROVIDE: gfsd
# REQUIRE: NETWORKING SERVERS
# BEFORE: DAEMON
# KEYWORD: FreeBSD shutdown

#
# Add the following lines to /etc/rc.conf to enable gfsd:
# gfsd_enable (bool):      Set to "NO" by default.
# gfsd_flags (str):        Set to "" by default.
#                             Extra flags passed to start command
#
. %%RC_SUBR%%

name="gfsd"
rcvar=`set_rcvar`

command="%%PREFIX%%/sbin/${name}"
pidfile="/var/run/${name}.pid"
required_files=%%SYSCONFDIR%%/gfarm.conf
# add more flags through ${${name}_flags}
command_args="-P /var/run/${name}.pid"

[ -z "$gfsd_enable" ]       && gfsd_enable="NO"
[ -z "$gfsd_flags" ]        && gfsd_flags=""

load_rc_config $name

GLOBUS_LOCATION=/usr/local/globus
export GLOBUS_LOCATION
if [ -f $GLOBUS_LOCATION/etc/globus-user-env.sh ]; then
. $GLOBUS_LOCATION/etc/globus-user-env.sh
fi

run_rc_command "$1"
