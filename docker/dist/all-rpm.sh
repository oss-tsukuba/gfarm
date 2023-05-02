#!/bin/sh
set -xeu

(cd && sh gfarm/docker/dist/mkrpm.sh)
sh ./install-rpm.sh
sh ./cert.sh
sh ./usercert.sh
sh ./config.sh
sh ./check.sh
(cd ~/gfarm && PKG=gfarm2fs sh docker/dist/mkrpm.sh)
PKG=gfarm2fs sh ./install-rpm.sh
echo All set
