#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

# remove #1
{
	rm -f ${MOUNTPOINT}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${MOUNTPOINT}/file
	if [ $? == 0 ]; then
		exit $exit_fail
	fi

	ls ${MOUNTPOINT}/${DIR1}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# remove #2
{
	rm -f ${MOUNTPOINT}/${DIR1}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${MOUNTPOINT}/${DIR1}/file
	if [ $? == 0 ]; then
		exit $exit_fail
	fi

	ls ${MOUNTPOINT}/${DIR1}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# remove #3
{
	rm -f ${MOUNTPOINT}/${DIR1}/${DIR2}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${MOUNTPOINT}/${DIR1}/${DIR2}/file
	if [ $? == 0 ]; then
		exit $exit_fail
	fi

	ls ${MOUNTPOINT}/${DIR1}/${DIR2}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# remove #4
{
	rm -rf ${MOUNTPOINT}/${DIR1}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
