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
FILE=${name}
rcvar=`set_rcvar`

command="%%PREFIX%%/sbin/${name}"
pidfile="/var/run/${FILE}.pid"
required_files=%%SYSCONFDIR%%/gfarm.conf
# add more flags through ${${name}_flags}
command_args="-P $pidfile"
globus_location="%%GLOBUS_LOCATION%%"

[ -z "$gfmd_enable" ]       && gfmd_enable="NO"
[ -z "$gfmd_flags" ]        && gfmd_flags=""

load_rc_config ${FILE}

if [ -z "${GLOBUS_LOCATION-}" ] && [ -n "$globus_location" ] &&
   [ -f "$globus_location/etc/globus-user-env.sh" ]
then
	GLOBUS_LOCATION="$globus_location"
	export GLOBUS_LOCATION
	. "$GLOBUS_LOCATION/etc/globus-user-env.sh"
fi

run_rc_command "$1"
