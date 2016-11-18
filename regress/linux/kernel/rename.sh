#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

rename_test_file() {
	dirsrc=$1
	filesrc=$1/$2
	dirdst=$3
	filedst=$3/$4

	rm -f ${filesrc} ${filedst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	touch ${filesrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${filesrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${filedst}
	if [ $? == 0 ]; then
		exit $exit_fail
	fi

	ls ${dirsrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	if [ "$dirsrc" != "$dirdst" ]; then
		ls ${dirdst}
		if [ $? != 0 ]; then
			exit $exit_fail
		fi
	fi

	mv ${filesrc} ${filedst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${filesrc}
	if [ $? == 0 ]; then
		exit $exit_fail
	fi

	stat ${filedst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	ls ${dirsrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	if [ "$dirsrc" != "$dirdst" ]; then
		ls ${dirdst}
		if [ $? != 0 ]; then
			exit $exit_fail
		fi
	fi

	rm -f ${filesrc} ${filedst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# rename #1
{
	rename_test_file ${MOUNTPOINT} file.src ${MOUNTPOINT} file.dst
}

# rename #2
{
	mkdir -p ${MOUNTPOINT}/dir1
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	rename_test_file ${MOUNTPOINT}/dir1 file.src ${MOUNTPOINT} file.dst
}

# rename #3
{
	rename_test_file ${MOUNTPOINT} file.src ${MOUNTPOINT}/dir1 file.dst
}

###############################################################


rename_test_dir() {
	dirsrc=$1
	filesrc=$1/$2
	dirdst=$3
	filedst=$3/$4

	rm -rf ${filesrc} ${filedst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	mkdir ${filesrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${filesrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${filedst}
	if [ $? == 0 ]; then
		exit $exit_fail
	fi

	ls ${dirsrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	if [ "$dirsrc" != "$dirdst" ]; then
		ls ${dirdst}
		if [ $? != 0 ]; then
			exit $exit_fail
		fi
	fi

	mv ${filesrc} ${filedst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	stat ${filesrc}
	if [ $? == 0 ]; then
		exit $exit_fail
	fi

	stat ${filedst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	ls ${dirsrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	if [ "$dirsrc" != "$dirdst" ]; then
		ls ${dirdst}
		if [ $? != 0 ]; then
			exit $exit_fail
		fi
	fi

	rm -rf ${filesrc} ${filedst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}

# rename #4
{
	rename_test_dir ${MOUNTPOINT} dir.src ${MOUNTPOINT} dir.dst
}

# rename #5
{
	mkdir -p ${MOUNTPOINT}/dir1
	if [ $? != 0 ]; then
		exit $exit_fail
	fi

	rename_test_dir ${MOUNTPOINT}/dir1 dir.src ${MOUNTPOINT} dir.dst
}

# rename #6
{
	rename_test_dir ${MOUNTPOINT} dir.src ${MOUNTPOINT}/dir1 dir.dst
}
