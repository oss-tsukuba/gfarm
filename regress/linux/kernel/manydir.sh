#!/bin/sh

if [ $# -lt 3 ]; then
	echo "Usage: manydir.sh [mkdir|rmdir] dirprefix loopnum"
	exit 1
fi

op=$1
prefix=$2
n=$3

i=1
while [ $i -le $n ]; do
	$op "$prefix$i"
	if [ $? != 0 ]; then
		echo "ERROR: $op $prefix$i failed"
		exit 1 
	fi
	i=`expr $i + 1`
done
