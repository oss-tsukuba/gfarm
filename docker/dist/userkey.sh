#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

KEY=.gfarm_shared_key
gfkey -f -p 31536000
gfarm-pcp -p ~/$KEY .

status=0
