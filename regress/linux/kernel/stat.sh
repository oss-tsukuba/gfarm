#!/bin/sh

. ./regress.conf
. ./linux/kernel/dironlytest-init.sh

# stat #1
{
	if [ -d "${MOUNTPOINT}/dir" ]; then
		rmdir ${MOUNTPOINT}/dir
	fi
	mkdir ${MOUNTPOINT}/dir
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	stat ${MOUNTPOINT}/dir > ${TMPFILE}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	timestamp=`date '+%Y-%m-%d %H:%M:%S'`

	line=`head -1 ${TMPFILE}`
	if ! expr "$line" : "  File: \`${MOUNTPOINT}/dir'" > /dev/null; then
		echo "WARN: stat line 1 maybe incorrect"
	fi

	line=`head -2 ${TMPFILE} | tail -1`
	if ! expr "$line" : "  Size: 0.*Blocks: 0.*directory" > /dev/null; then
		echo "WARN: stat line 2 maybe incorrect"
	fi

	line=`head -3 ${TMPFILE} | tail -1`
	if ! expr "$line" : ".*Links: 2" > /dev/null; then
		echo "WARN: stat line 3 maybe incorrect"
	fi

	line=`head -4 ${TMPFILE} | tail -1`
	if ! expr "$line" : "Access: (0755/drwxr-xr-x).*Uid: ( *${TESTUID}/ *${TESTUSER}).*Gid: ( *${TESTGID}/ *${TESTGROUP})" > /dev/null; then
		echo "WARN: stat line 4 maybe incorrect"
	fi

	line=`head -5 ${TMPFILE} | tail -1`
	if ! expr "$line" : "Access: ${timestamp}\." > /dev/null; then
		echo "WARN: stat line 5 maybe incorrect"
	fi
	line=`head -6 ${TMPFILE} | tail -1`
	if ! expr "$line" : "Modify: ${timestamp}\." > /dev/null; then
		echo "WARN: stat line 6 maybe incorrect"
	fi
	line=`head -7 ${TMPFILE} | tail -1`
	if ! expr "$line" : "Change: ${timestamp}\." > /dev/null; then
		echo "WARN: stat line 7 maybe incorrect"
	fi

	rmdir ${MOUNTPOINT}/dir
}
