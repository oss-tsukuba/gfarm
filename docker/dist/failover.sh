#!/bin/sh
set -xeu

: ${ASAN_OPTIONS=halt_on_error=false:log_exe_name=true:log_path=/var/tmp/gfarm.log.asan}
LSAN_SUPP=$(cd $(dirname $0) && pwd)/lsan.supp
: ${LSAN_OPTIONS=halt_on_error=false:log_exe_name=true:log_path=/var/tmp/gfarm.log.lsan:suppressions=$LSAN_SUPP}
: ${UBSAN_OPTIONS=halt_on_error=false:log_exe_name=true:log_path=/var/tmp/gfarm.log.ubsan}
: ${TSAN_OPTIONS=halt_on_error=false:log_exe_name=true:log_path=/var/tmp/gfarm.log.tsan}
export ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS TSAN_OPTIONS

dir=manual/lib/libgfarm/gfarm/gfs_pio_failover

grid-proxy-init -q || :

gfmkdir -p /tmp
gfchmod 1777 /tmp || :

TOP=~/gfarm
BUILD=$TOP/build-$(gfarm.arch.guess)
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
