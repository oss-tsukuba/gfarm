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
required_files=%%PREFIX%%/etc/gfarm.conf
# add more flags through ${${name}_flags}
command_args="-f ${required_files} -P /var/run/${name}.pid"

[ -z "$gfmd_enable" ]       && gfmd_enable="NO"
[ -z "$gfmd_flags" ]        && gfmd_flags=""

load_rc_config $name

run_rc_command "$1"
