#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

# mkdir #1
{
	mkdir ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	mkdir ${MOUNTPOINT}/BB
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	mkdir ${MOUNTPOINT}/CCC
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# mkdir #2
{
	mkdir ${MOUNTPOINT}/A/a
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	mkdir ${MOUNTPOINT}/BB/bb
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	mkdir ${MOUNTPOINT}/CCC/ccc
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# mkdir #3
{
	mkdir ${MOUNTPOINT}/A/a/X
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	mkdir ${MOUNTPOINT}/BB/bb/YY
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	mkdir ${MOUNTPOINT}/CCC/ccc/ZZZ
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# mkdir #4
{
	# must success
	mkdir ${MOUNTPOINT}/${LONGNAME255}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	# must fail
	mkdir ${MOUNTPOINT}/${LONGNAME256}
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
}

# mkdir #5 (recreate, must fail)
{
	mkdir ${MOUNTPOINT}/A
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
	mkdir ${MOUNTPOINT}/A/a
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
	mkdir ${MOUNTPOINT}/A/a/X
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
}
