#!/bin/sh
set -xeu

dir=manual/lib/libgfarm/gfarm/gfs_pio_failover

grid-proxy-init -q || :

gfmkdir -p /tmp
gfchmod 1777 /tmp || :

TOP=~/gfarm
BUILD=$TOP/build
MAKE=$TOP/makes/make.sh
cd $BUILD/regress
$MAKE all

cd ${dir}
rm -f gfmd-failover-local.sh
ln -s $TOP/regress/${dir}/gfmd-failover-local.systemd.sh \
	gfmd-failover-local.sh

: ${USER:=$(id -un)}
priv=$USER SUDO=sudo gfsudo $MAKE check

rm gfmd-failover-local.sh

echo Done
