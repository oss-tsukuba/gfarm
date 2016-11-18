#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

# rmdir #1
{
	rmdir ${MOUNTPOINT}/A/a/X
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	rmdir ${MOUNTPOINT}/BB/bb/YY
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	rmdir ${MOUNTPOINT}/CCC/ccc/ZZZ
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# rmdir #2
{
	rmdir ${MOUNTPOINT}/A/a
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	rmdir ${MOUNTPOINT}/BB/bb
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	rmdir ${MOUNTPOINT}/CCC/ccc
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# rmdir #3
{
	rmdir ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	rmdir ${MOUNTPOINT}/BB
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	rmdir ${MOUNTPOINT}/CCC
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# rmdir #4
{
	# must success
	rmdir ${MOUNTPOINT}/${LONGNAME255}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	# must fail
	rmdir ${MOUNTPOINT}/${LONGNAME256}
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
}

# rmdir #5 (rmdir deleted, must fail)
{
	rmdir ${MOUNTPOINT}/A
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
	rmdir ${MOUNTPOINT}/A/a
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
	rmdir ${MOUNTPOINT}/A/a/X
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
}
