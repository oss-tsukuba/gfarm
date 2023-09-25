#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

: ${USER:=$(id -un)}
SUDO=
[ $# -eq 0 ] && u=$USER || {
	u=$1; SUDO="sudo -u $u"
}

[ -f /home/$u/.globus/usercert.pem ] && {
	status=0
	exit 0
}

id $u > /dev/null
$SUDO grid-cert-request -cn $u -nopw > /dev/null 2>&1
sh ./cert-sign.sh /home/$u/.globus/usercert_request.pem \
	/home/$u/.globus/usercert.pem

$SUDO gfarm-pcp -p /home/$u/.globus .

status=0
