#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

# readdir 1 (empty)
{
	/bin/ls ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -l ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -la ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	# link count maybe 2 or 3 (include/exclude lost+found directory)
	n=`/bin/ls -la ${MOUNTPOINT} | grep "^drwxrwxr-x.\+[23] \+${ADMUSER} \+${ADMGROUP} .\+ \.$" | wc -l`
	if [ "${n}" -ne 1 ]; then
		echo "timestamp maybe incorrect"
	fi
}

# readdir 2 (1 dir)
{
	mkdir ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	timestampA=`date '+%Y-%m-%d %H:%M:%S.[0-9]\+ +0900'`

	/bin/ls ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -l ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	# NOTE: use --full-time for timestamp check below
	/bin/ls -la --full-time ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	/bin/ls ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -l ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -la ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	n=`/bin/ls -l --full-time ${MOUNTPOINT} | grep "^drwxr-xr-x.\+2 \+${TESTUSER} \+${TESTGROUP} \+0 ${timestampA} A$" | wc -l`
	if [ "${n}" -ne 1 ]; then
		echo "WARN: timestamp maybe incorrect"
	fi
	n=`/bin/ls -la --full-time ${MOUNTPOINT} | grep "^drwxrwxr-x.\+[34] \+${ADMUSER} \+${ADMGROUP} \+. ${timestampA} \.$" | wc -l`
	if [ "${n}" -ne 1 ]; then
		echo "WARN: timestamp maybe incorrect"
	fi
}

# readdir 3 (2 dir)
{
	sleep 2
	mkdir ${MOUNTPOINT}/BB
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	timestampBB=`date '+%Y-%m-%d %H:%M:%S.[0-9]\+ +0900'`
	/bin/ls ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -l ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	# NOTE: use --full-time for timestamp check below
	/bin/ls -la --full-time ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	/bin/ls ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -l ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -la ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	n=`/bin/ls -l --full-time ${MOUNTPOINT} | grep "^drwxr-xr-x.\+2 \+${TESTUSER} \+${TESTGROUP} \+0 \+${timestampA} A$" | wc -l`
	if [ "${n}" -ne 1 ]; then
		echo "WARN: timestamp maybe incorrect"
	fi

	/bin/ls ${MOUNTPOINT}/BB
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -l ${MOUNTPOINT}/BB
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -la ${MOUNTPOINT}/BB
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	n=`/bin/ls -l --full-time ${MOUNTPOINT} | grep "^drwxr-xr-x.\+2 \+${TESTUSER} \+${TESTGROUP} \+0 \+${timestampBB} BB$" | wc -l`
	if [ "${n}" -ne 1 ]; then
		echo "WARN: timestamp maybe incorrect"
	fi
	n=`/bin/ls -la --full-time ${MOUNTPOINT} | grep "^drwxrwxr-x.\+[45] \+${ADMUSER} \+${ADMGROUP} \+. ${timestampBB} \.$" | wc -l`
	if [ "${n}" -ne 1 ]; then
		echo "WARN: timestamp maybe incorrect"
	fi
}

# readdir 4 (1 dir, after rmdir)
{
	sleep 2
	rmdir ${MOUNTPOINT}/BB
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	timestampBB=`date '+%Y-%m-%d %H:%M:%S.[0-9]\+ +0900'`
	/bin/ls ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -l ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	# NOTE: use --full-time for timestamp check below
	/bin/ls -la --full-time ${MOUNTPOINT}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	/bin/ls ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -l ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	/bin/ls -la ${MOUNTPOINT}/A
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	n=`/bin/ls -l --full-time ${MOUNTPOINT} | grep "^drwxr-xr-x.\+2 \+${TESTUSER} \+${TESTGROUP} \+0 \+${timestampA} A$" | wc -l`
	if [ "${n}" -ne 1 ]; then
		echo "WARN: timestamp maybe incorrect"
	fi

	n=`/bin/ls -la --full-time ${MOUNTPOINT} | grep "^drwxrwxr-x.\+[34] \+${ADMUSER} \+${ADMGROUP} \+. ${timestampBB} \.$" | wc -l`
	if [ "${n}" -ne 1 ]; then
		echo "WARN: timestamp maybe incorrect"
	fi

	# must fail
	echo "/bin/ls of removed directory (must fail)"
	/bin/ls ${MOUNTPOINT}/BB
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
	
	rmdir ${MOUNTPOINT}/A
}
