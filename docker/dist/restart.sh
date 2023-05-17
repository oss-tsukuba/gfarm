#!/bin/sh
set -xeu
status=1
PROG=$(basename $0)
trap 'echo NG: $PROG; exit $status' 1 2 15

gfarm-prun -a -p "sudo systemctl restart gfmd || :"
gfarm-prun -a -p "sudo systemctl restart gfsd || :"

echo Done
