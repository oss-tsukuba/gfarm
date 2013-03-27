#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

# create #1
{
	touch ${MOUNTPOINT}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${MOUNTPOINT}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	ls ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# create #2
{
	mkdir -p ${MOUNTPOINT}/${DIR1}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	touch ${MOUNTPOINT}/${DIR1}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${MOUNTPOINT}/${DIR1}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	ls ${MOUNTPOINT}/${DIR1}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# create #3
{
	mkdir -p ${MOUNTPOINT}/${DIR1}/${DIR2}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	touch ${MOUNTPOINT}/${DIR1}/${DIR2}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${MOUNTPOINT}/${DIR1}/${DIR2}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	ls ${MOUNTPOINT}/${DIR1}/${DIR2}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
