#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

trap 'exit $exit_trap' $trap_sigs

user=`whoami`
if [ "$user" != "root" ]; then
	echo "This test scripts must run by root"
	exit 1
fi

orglang=`echo $LANG`
export LANG=C
orgumask=`umask`
umask 0022

make -C linux/kernel/src

echo "*** start Linux kernel module (File/Directory I/O) test. some tests issue error messages. ***"

start_mount

echo "*** start create tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/create.sh

echo "*** start remove tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/remove.sh

echo "*** start hardlink tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/hardlink.sh

echo "*** start rename tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/rename.sh

echo "*** start stat tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/stat.sh

echo "*** start file I/O (read/write/seek, truncate, mmap) tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/fileio.sh

echo "*** start flock tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/flock.sh

stop_mount

echo
echo
echo "Test done!"
echo

export LANG="$orglang"
umask ${orgumask}
