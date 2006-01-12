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
FILE=${name}
rcvar=`set_rcvar`

command="%%PREFIX%%/sbin/${name}"
pidfile="/var/run/${FILE}.pid"
required_files=%%SYSCONFDIR%%/gfarm.conf
# add more flags through ${${name}_flags}
command_args="-P $pidfile"
globus_location="%%GLOBUS_LOCATION%%"

[ -z "$gfsd_enable" ]       && gfsd_enable="NO"
[ -z "$gfsd_flags" ]        && gfsd_flags=""

load_rc_config ${FILE}

if [ -z "${GLOBUS_LOCATION-}" ] && [ -n "$globus_location" ] &&
   [ -f "$globus_location/etc/globus-user-env.sh" ]
then
	GLOBUS_LOCATION="$globus_location"
	export GLOBUS_LOCATION
	. "$GLOBUS_LOCATION/etc/globus-user-env.sh"
fi

run_rc_command "$1"
