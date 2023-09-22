#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

u=$1
SUDO="sudo -u $u"
gfarm-prun -a -p sudo useradd -m -s /bin/bash $u
gfarm-prun -a -p sudo passwd -d $u
$SUDO sh ./setup.sh
$SUDO gfarm-pcp /home/$u/.nodelist .

sh ./userkey.sh $u
sh ./usercert.sh $u

SUB=$($SUDO grid-proxy-info -identity)
gfuser -c $u $u / $SUB

status=0
