#!/bin/sh

export LANG=C
if [ $# -lt 3 ]; then
        echo "Usage: dirdepth.sh dir num depth"
        echo "creating depth directory and num files in each directory at dir"
        exit 1
fi
#. ./regress.conf
#. ./linux/kernel/initparams.sh
export TIMEFORMAT="    real:%3lR       user:%3lU       sys:%3lS"

prefix=$1
num=$2
depth=$3
fmt="fffffffffffffff%08d"

echo "==========$$ `date` $prefix $num $depth =============="
time rm -rf $prefix
mkdir -p $prefix

findfile()
{
	local expect=$1
	local opt=$2
	local found

	echo "$BASHPID find $opt"
	time found=`find $opt |wc -l`
	if [ $found -ne $expect ]; then
		if [ $$ -eq $BASHPID ]; then
		echo "ERROR:$BASHPID found $found not equal $expect, $opt"
		exit 1
		fi
	fi
}
rmfiles()
{
	local opt=$1
	echo "$BASHPID removing $opt"
	time find $opt -exec rm {} \;
	if [ $? -ne 0 ]; then
		echo "ERROR:$BASHPID rm $opt fail"
		exit 1
	fi
}
rmfilels()
{
	local dir=$1
	local num=$2

	local name=$(printf $fmt $num)
	rmfiles "$dir -name $name"
	findfile 0 "$dir -name $name"
}

lsfile()
{
	local expect=$1
	local opt=$2
	local found

	echo "$BASHPID ls $opt"
	time found=`ls $opt |wc -l`
	if [ $found -ne $expect ]; then
		echo "ERROR:$BASHPID result $found not equal $expect"
		exit 1
	fi
}


#---------------------------------------------------
echo "creating $num files in $depth at $prefix"
time sh ./linux/kernel/manyfile.sh \
	touch $prefix $fmt $num $depth
#---------------------------------------------------
echo "finding file type file or dir"
findfile $(( $depth + 1))  "$prefix -type d" &
findfile $(( $depth * $num))  "$prefix -type f" &
name=$(printf $fmt 1)
findfile $depth "$prefix -name $name"
wait
echo "jobs are $(jobs -l) running"
#---------------------------------------------------
echo "removing file and count files ======================"
for i in `seq 10`; do
	num=$(( $num - 1 ))
	rmfilels  $prefix $i &
	num=$(( $num - 1 ))
	j=$(( $i + 10 ))
	name=$(printf $fmt $j)
	findfile $depth "$prefix -name $name"
	j=$(( $i + 100 ))
	rmfilels  $prefix $j &
done
wait
echo "jobs are $(jobs -l) running"
#---------------------------------------------------
echo "finding file type file or dir, after removfiles ======================"

findfile $(( $depth + 1))  "$prefix -type d"
findfile $(( $depth * $num))  "$prefix -type f"

#---------------------------------------------------
rmall(){
	echo "removing $num files in $depth at $prefix"
	time rm -rf $prefix
	if [ -a $prefix ]; then
		echo "ERROR: still exist $prefix"
		exit 1
	fi
}
#rmall
#---------------------------------------------------
echo "End of dirdepth ==========================="
