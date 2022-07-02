#!/bin/sh

# this script is called from up.rc

progname=`basename $0`
HOST_SHARE_DIR=/mnt
hook_dir="${HOST_SHARE_DIR}/hook"

case $# in
2)	hook="${hook_dir}/${1}.hook"
	service="${2}.service"
	;;
*)	echo >&2 "Usage: ${progname} <hook_name> <systemd_unit_file_basename>"
	exit 2
	;;
esac

unit_file_modify()
{
    unit_file=$1

    if [ -f "${unit_file}" ]
    then
        [ -f "${unit_file}.bak" ] || cp -p "${unit_file}" "${unit_file}.bak"
        sed "/^ExecStart=/s||ExecStart=${hook} |" "${unit_file}.bak" \
            >"${unit_file}"
    fi
}

unit_file_modify "/etc/systemd/system/${service}"
systemctl daemon-reload
systemctl restart "${service}"
