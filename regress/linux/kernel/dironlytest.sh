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

start_mount

echo "*** start mkdir tests ***"
sudo -u ${TESTUSER} gfrm -rf A BB CCC
sudo -u ${TESTUSER} sh -x ./linux/kernel/mkdir.sh

echo "*** start rmdir tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/rmdir.sh

echo "*** start readdir tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/readdir.sh

echo "*** start getattr tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/stat.sh

echo "*** start statfs tests ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/statfs.sh

echo "*** start multiprocess test ***"
sudo -u ${TESTUSER} sh -x ./linux/kernel/multiproc.sh

echo "*** start multiuser test ***"
sh -x ./linux/kernel/multiuser.sh

sleep 3

stop_mount

echo "Test done!"

export LANG="$orglang"
umask ${orgumask}
