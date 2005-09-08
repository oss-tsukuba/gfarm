#!/bin/sh
#
# $FreeBSD$
#

# PROVIDE: gfmd
# REQUIRE: NETWORKING SERVERS
# BEFORE: DAEMON
# KEYWORD: FreeBSD shutdown

#
# Add the following lines to /etc/rc.conf to enable gfmd:
# gfmd_enable (bool):      Set to "NO" by default.
# gfmd_flags (str):        Set to "" by default.
#                             Extra flags passed to start command
#
. %%RC_SUBR%%

name="gfmd"
rcvar=`set_rcvar`

command="%%PREFIX%%/sbin/${name}"
pidfile="/var/run/${name}.pid"
required_files=%%SYSCONFDIR%%/gfarm.conf
# add more flags through ${${name}_flags}
command_args="-P /var/run/${name}.pid"

[ -z "$gfmd_enable" ]       && gfmd_enable="NO"
[ -z "$gfmd_flags" ]        && gfmd_flags=""

load_rc_config $name

if [ -n "%%GLOBUS_LOCATION%%" ]; then
	GLOBUS_LOCATION=%%GLOBUS_LOCATION%%
	export GLOBUS_LOCATION
	if [ -f "$GLOBUS_LOCATION/etc/globus-user-env.sh" ]; then
		. "$GLOBUS_LOCATION/etc/globus-user-env.sh"
	fi
fi

run_rc_command "$1"
