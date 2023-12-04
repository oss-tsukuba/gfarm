#!/bin/sh
set -xeu

dir=manual/lib/libgfarm/gfarm/gfs_pio_failover

grid-proxy-init -q || :

gfmkdir -p /tmp
gfchmod 1777 /tmp || :

mkdir -p ~/gfarm/build/regress
cd ~/gfarm/build/regress
~/gfarm/makes/make.sh all

cd ${dir}
rm -f gfmd-failover-local.sh
ln -s ~/gfarm/regress/${dir}/gfmd-failover-local.systemd.sh \
	gfmd-failover-local.sh

: ${USER:=$(id -un)}
priv=$USER SUDO=sudo gfsudo ~/gfarm/makes/make.sh check

rm gfmd-failover-local.sh

echo Done
