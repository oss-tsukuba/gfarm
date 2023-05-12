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

KEY=.gfarm_shared_key
$SUDO gfkey -f -p 31536000
$SUDO gfarm-pcp -p /home/$u/$KEY .

status=0
