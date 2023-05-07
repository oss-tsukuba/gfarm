#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

u=$1
gfarm-prun -a -p sudo useradd -m $u
gfarm-prun -a -p sudo passwd -d $u
sudo -u $u sh ./setup.sh

sh ./userkey.sh $u
sh ./usercert.sh $u

status=0
