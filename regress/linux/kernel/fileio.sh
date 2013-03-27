#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

# read/write/seek etc
{
	./linux/kernel/src/readwrite ${MOUNTPOINT}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# truncate
{
	./linux/kernel/src/truncate ${MOUNTPOINT}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# mmap
{
	./linux/kernel/src/mmap ${MOUNTPOINT}/file
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
