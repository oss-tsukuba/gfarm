#!/bin/sh

export LANG=C
if [ $# -lt 2 ]; then
        echo "Usage: dirremote.sh dir peer [msec]"
        echo "sdirectory operation with peer"
        exit 1
fi
export TIMEFORMAT="    real:%3lR       user:%3lU       sys:%3lS"

prefix=$1
peer=$2
msec=1000
if [ $# -gt 2 ]; then
	msec=$3
fi
fmt="fffffffffffffff%08d"

echo "==========$$ `date` $prefix $peer $msec =============="
rm -rf $prefix
mkdir -p $prefix
usec=$(($msec * 1000))

findfile()
{
	local expect=$1
	local opt=$2
	local found

	found=`find $opt |wc -l`
	if [ $found -ne $expect ]; then
		if [ $$ -eq $BASHPID ]; then
		echo "ERROR:$BASHPID found $found not equal $expect, $opt"
		exit 1
		fi
	fi
}
fileact()
{
	local action="$1"
	local file=$prefix/$2
	$action $file
	if [ $? -ne 0 ]; then
		echo "ERROR:$action $file fail"
		exit 1
	fi
}
peeract()
{
	local action="$1"
	local file=$prefix/$2
	ssh $peer $action $file
	if [ $? -ne 0 ]; then
		echo "ERROR:ssh $peer $action $file fail"
		exit 1
	fi
}

#---------------------------------------------------
#---------------------------------------------------
echo "local :creating file"
fileact mkdir dir1
fileact touch dir1/file1
fileact touch dir1/file2
fileact touch dir1/file3
fileact mkdir dir1/file4
fileact mkdir dir1/file5

echo "remote:remove"
peeract rm	dir1/file1
peeract rm	dir1/file2
peeract rm	dir1/file3
peeract rmdir	dir1/file4
peeract rmdir	dir1/file5

echo "local:sllep $msec, list and create"
usleep $usec
fileact "ls -liaF" dir1/
fileact touch dir1/file1
fileact touch dir1/file2
fileact mkdir dir1/file3
fileact mkdir dir1/file4
fileact touch dir1/file5

echo "remote:list and remove"
peeract "ls -liaF" dir1
peeract rm dir1/file1
peeract rm dir1/file2
peeract rmdir dir1/file3
peeract rmdir dir1/file4
peeract rm dir1/file5

echo "local:list"
fileact "ls -liaF" dir1/
