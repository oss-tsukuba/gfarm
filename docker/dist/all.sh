#!/bin/sh
set -xeu

(cd && sh gfarm/docker/dist/install.sh)
sh ./cert.sh
sh ./usercert.sh
sh ./config.sh
sh ./check.sh
(cd ~/gfarm && PKG=gfarm2fs sh docker/dist/install.sh)
echo All set
