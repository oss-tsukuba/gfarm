#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

stat_test() {
	file=$1
	type=$2
	nlink=$3
	access=$4

	stat ${file} > ${TMPFILE}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	timestamp=`date '+%Y-%m-%d %H:%M:%S'`

	line=`head -1 ${TMPFILE}`
	if ! expr "$line" : "  File: \`${file}'" > /dev/null; then
		echo "WARN: stat line 1 maybe incorrect"
	fi

	line=`head -2 ${TMPFILE} | tail -1`
	if ! expr "$line" : ".*${type}" > /dev/null; then
		echo "WARN: stat line 2 maybe incorrect"
	fi

	line=`head -3 ${TMPFILE} | tail -1`
	if ! expr "$line" : ".*Links: ${nlink}" > /dev/null; then
		echo "WARN: stat line 3 maybe incorrect"
	fi

	line=`head -4 ${TMPFILE} | tail -1`
	if ! expr "$line" : "Access: (${access}).*Uid: ( *${TESTUID}/ *${TESTUSER}).*Gid: ( *${TESTGID}/ *${TESTGROUP})" > /dev/null; then
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
}

# stat #1
{
	testdir=${MOUNTPOINT}/${DIR1}
	
	if [ -d "${testdir}" ]; then
		rm -rf ${testdir}
	fi

	mkdir ${testdir}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	stat_test ${testdir} "directory" 2 "0755/drwxr-xr-x"
	
	rm -rf ${testdir}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# stat #2
{
	testfile=${MOUNTPOINT}/file
	
	if [ -e "${testfile}" ]; then
		rm -rf ${testfile}
	fi

	touch ${testfile}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	stat_test ${testfile} "regular empty file" 1 "0644/-rw-r--r--"
	
	rm ${testfile}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
