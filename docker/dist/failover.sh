#!/bin/sh
set -xeu

grid-proxy-init -q || :

gfmkdir -p /tmp
gfchmod 1777 /tmp || :

cd ~/gfarm/regress
make all

cd manual/lib/libgfarm/gfarm/gfs_pio_failover
rm -f gfmd-failover-local.sh
ln -s gfmd-failover-local.systemd.sh gfmd-failover-local.sh

: ${USER:=$(id -un)}
priv=$USER SUDO=sudo gfsudo ./test-all.sh auto

rm gfmd-failover-local.sh

echo Done
