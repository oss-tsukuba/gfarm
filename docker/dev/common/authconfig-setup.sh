#!/bin/sh

# this script is called from up.rc

case $# in
1)	;;
*)	echo >&2 "Usage: `basename $0` client"
	echo >&2 "Usage: `basename $0` gfmd"
	echo >&2 "Usage: `basename $0` gfsd"
	exit 2;;
esac

if [ -f /usr/local/etc/gfarm2.conf ]; then
    conf_dir=/usr/local/etc
elif [ -f /etc/gfarm2.conf ]; then
    conf_dir=/etc
else
    echo >&2 "$0: FATAL ERROR: cannot find gfarm2.conf"
    exit 2
fi

conf_modify()
{
    file="${conf_dir}/$1"
    type=$2

    if [ -f "${file}" ]
    then
        [ -f "${file}.bak" ] || cp -p "${file}" "${file}.bak"
        (
            sed '
                /^[ 	]*auth[ 	]/d
                /^[ 	]*spool_server_cred/d
            ' "${file}.bak"
            echo "include /mnt/conf/auth-${type}.conf"
        ) >"${file}"
    else
        echo >&2 "$0: ERROR: ${file} not found"
    fi
}

host_type=$1
case ${host_type} in
client)	# the following setting is not normally used,
	# because $HOME/.gfarm2rc does the job.
	conf_modify gfarm2.conf client
	;;
gfmd)	conf_modify gfarm2.conf client
	conf_modify gfmd.conf gfmd
	;;
gfsd)	conf_modify gfarm2.conf gfsd
	;;
*)
	echo >&2 "$0: unknown host type \"${host_type}\""
	exit 2
	;;
esac
