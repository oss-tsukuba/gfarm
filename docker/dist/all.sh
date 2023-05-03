#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

(cd && sh gfarm/docker/dist/install.sh)
sh ./cert.sh
sh ./usercert.sh
sh ./config.sh
sh ./check.sh
(cd ~/gfarm && PKG=gfarm2fs sh docker/dist/install.sh)
status=0
echo All set
