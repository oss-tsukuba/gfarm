#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

# statfs 1
{
	df ${MOUNTPOINT} > ${TMPFILE}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	line=`tail -1 ${TMPFILE}`
	if expr "$line" : "/dev/gfarm *0 *0 *0 *- *${MOUNTPOINT}" > /dev/null; then
		echo "no gfsd running, used/avail blocks are all zero. statfs OK."
	elif expr "$line" : "/dev/gfarm \+[0-9]\+ \+[0-9]\+ \+[0-9]\+ \+[0-9]\+\% ${MOUNTPOINT}" > /dev/null; then
		echo "gfsd running, statfs OK."
	else
		echo "WARN: df output maybe incorrect"
	fi
}
