#!/bin/sh

if [ $# -lt 5 ]; then
        echo "Usage: manyfile.sh touch dir fmt num depth"
        exit 1
fi

manyfile(){
	local op=$1
	local dir=$2
	local fmt=$3
	local num=$4
	local depth=$(( $5 -1 ))

	local name=$dir/$(printf $fmt 0)
	mkdir -p $name
	if [ $depth  -gt 0 ]; then
		manyfile $op $name $fmt $num $depth
	fi
	local i=1
	while [ $i -le $num ]; do
		name=$dir/$(printf $fmt $i)
		$op $name
		if [ $? != 0 ]; then
			echo "ERROR: $op $name failed"
			exit 1
		fi
		i=`expr $i + 1`
	done
}

manyfile $*
