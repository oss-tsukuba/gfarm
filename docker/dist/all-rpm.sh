#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

sh ./setup.sh
(cd && sh gfarm/docker/dist/mkrpm.sh)
sh ./install-rpm.sh
sh ./cert.sh
sh ./usercert.sh
sh ./config.sh
sh ./check.sh
(cd ~/gfarm && PKG=gfarm2fs sh docker/dist/mkrpm.sh)
PKG=gfarm2fs sh ./install-rpm.sh
status=0
echo All set
