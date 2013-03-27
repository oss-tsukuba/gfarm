#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

user=`whoami`
if [ "$user" != "root" ]; then
	echo "This test script must run by root"
	exit $exit_fail
fi

idinfo=`id ${TESTUSER2}`
if [ $? != 0 ]; then
	echo "Set ${TESTUSER2} in dironlytest-init.sh as another valid gfarm user"
	exit $exit_fail
fi
t=`grep "${TESTGROUP}" /etc/group | grep "${TESTUSER2}" | wc -l`
if [ "${t}" -ne 1 ]; then
	echo "Set ${TESTUSER2} in /etc/group as a member of ${TESTGROUP} group"
	exit $exit_fail
fi

n=100

# multiuser #1
{
		sudo -u ${TESTUSER}  mkdir ${MOUNTPOINT}/XXX
		sudo -u ${TESTUSER2} mkdir ${MOUNTPOINT}/YYY
		sudo -u ${TESTUSER}  ls ${MOUNTPOINT}
		sudo -u ${TESTUSER2} ls ${MOUNTPOINT}
}

# multiuser #2
{
		sudo -u ${TESTUSER}  rmdir ${MOUNTPOINT}/XXX
		sudo -u ${TESTUSER2} rmdir ${MOUNTPOINT}/YYY
		sudo -u ${TESTUSER}  ls ${MOUNTPOINT}
		sudo -u ${TESTUSER2} ls ${MOUNTPOINT}
}

# multiuser #3
{
		sudo -u ${TESTUSER}  sh -x ./linux/kernel/manydir.sh mkdir ${MOUNTPOINT}/A ${n} &
		sudo -u ${TESTUSER2} sh -x ./linux/kernel/manydir.sh mkdir ${MOUNTPOINT}/B ${n}
		wait $!

		sudo -u ${TESTUSER} ls ${MOUNTPOINT}
		sleep 3
}

# multiuser #4
{
		sudo -u ${TESTUSER}  sh -x ./linux/kernel/manydir.sh rmdir ${MOUNTPOINT}/A ${n} &
		sudo -u ${TESTUSER2} sh -x ./linux/kernel/manydir.sh rmdir ${MOUNTPOINT}/B ${n}
		wait $!

		sudo -u ${TESTUSER} ls ${MOUNTPOINT}
}
