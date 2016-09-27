#!/bin/sh

export LANG=C
export TIMEFORMAT='	real:%3lR	user:%3lU	sys:%3lS'

progdir=$(cd $(dirname $0) && pwd)
prog=$(basename $0)
progfile=$progdir/$prog
regressdir=$progdir/../..

. $progdir/initparams.sh


cmpfile=$regressdir/../doc/docbook/ja/ref/man5/gfarm2.conf.5.docbook
mountp=/tmp/mnt
tgtdir=/tmp/mnt/dir2
mb=1028
peer=dhcp057


clear_cache() {
	val=3
	echo "========== clearing cache ========================"
	sudo  sh -c "echo $val  > /proc/sys/vm/drop_caches"
	if [ $? != 0 ]; then
		echo "cache clear fail. do"
		echo "echo $val  > /proc/sys/vm/drop_caches"
		read -p "Type c to continue" REPLY
		if [ x$REPLY != "xc" ] ; then
			exit 0
		fi
	fi
}
prstat()
{
	echo "peer -------------------"
	ssh $peer cat /proc/fs/gfarm/27/ibstat
	echo "local -------------------"
	cat /proc/fs/gfarm/27/ibstat
}
cp1file(){
	local remote=$1
	local from=$2

	local ssh=""
	if [ $remote -ne 0 ]; then
		ssh="ssh $peer"
	fi
	echo "copying $ssh $from /dev/null"
	time $ssh dd if=$from bs=1048576 of=/dev/null
	if [ $? != 0 ]; then
		echo "ERROR: $ssh dd if=$from bs=1048576 of=/dev/null fail"
		exit 1
	fi
}
cp2file(){
	local remote=$1
	local from=$2
	local to=$3

	local ssh=""
	if [ $remote -ne 0 ]; then
		ssh="ssh $peer"
	fi
	echo "copying $ssh $from $to"
	time $ssh dd if=$from bs=1048576 of=$to
	if [ $? != 0 ]; then
		echo "ERROR: $ssh cp $from $to fail"
		exit 1
	fi
}
cmpfile()
{
	local remote=$1
	local from=$2
	local to=$3

	local ssh=""
	if [ $remote -ne 0 ]; then
		ssh="ssh $peer"
	fi
	echo "comparing $ssh $from $to"
	time $ssh cmp -l $from $to
	if [ $? != 0 ]; then
		echo "ERROR: $ssh cmp $from $to fail"
		exit 1
	fi
}


do_test()
{
	if [ $# -lt 4 ]; then
		echo "Usage: fileread.sh basedir dir size peer"
		exit 1
	fi

	mountp=$1
	tgtdir=$mountp/$2
	mb=$3
	peer=$4

	size=$(( $mb * 1048576 + 20 ))
	echo "========== `date` $tgtdir $mb $peer =============="
	rm -rf $tgtdir
	mkdir -p $tgtdir
	mkdir -p $mountp/testdata
	orgfile=$mountp/testdata/testdata."$mb"M

	clear_cache 1

	if [ -f $orgfile ]; then
		echo "using $orgfile"
	else
		if [ $# -gt 4 ]; then
			file=$5
		else
			file=$cmpfile
		fi
		csize=0
		fsize=$(stat --printf="%s" $file)
		echo "creating $orgfile with $file"
		while [ $csize -le $size ]; do
			dd conv=notrunc if=$file bs=1 seek=$csize of=$orgfile
			csize=$(( $csize + $fsize ))
		done
	fi
	truncate --size=$size  $orgfile
	ls -l $orgfile

	file=`mktemp --tmpdir=$tgtdir`
	echo "---- local to NULL	----------------------------------"
	cp1file 0 $orgfile
	echo "---- local to local	----------------------------------"
	cp2file 0 $orgfile $file
	echo "---- cache to NULL	----------------------------------"
	cp1file 0 $file
	echo "---- remote-cache to NULL	----------------------------------"
	cp1file 1 $file
	echo "---- cache to local	----------------------------------"
	cp2file 1 $file $file.1
	echo "---- cache to NULL	----------------------------------"
	cp1file 1 $file.1
	echo "---- remote-cache to NULL	----------------------------------"
	cp1file 0 $file.1
	echo "---- remote-cache and local-cache	--------------------------"
	cmpfile 0 $orgfile $file.1
	echo "---- removing files	----------------------------------"
	time rm -f $file $file.1
	echo "End of fileread"
}
do_prepare()
{
	peer=$1
	mp=$2
	sudo sh $progfile mount $mp
	ssh $peer sudo sh $progfile mount $mp
}
do_cleanoff()
{
	peer=$1
	mp=$2
	sudo sh $progfile umount $mp
	ssh $peer sudo sh $progfile umount $mp
}
do_ibset()
{
	peer=$1
	id1=$(ls /proc/fs/gfarm/ | head -1)
	id2=$(ssh $peer ls /proc/fs/gfarm/ | head -1)
	ib1=$(cat /proc/fs/gfarm/$id1/ibaddr | head -1)
	ib2=$(ssh $peer "cat /proc/fs/gfarm/$id2/ibaddr | head -1")
	sudo sh -c "echo $ib2 > /proc/fs/gfarm/$id1/ibaddr"
	ssh $peer sudo sh -c "\"echo $ib1 > /proc/fs/gfarm/$id2/ibaddr\""
}
ibtest()
{
	if [ $# -lt 4 ]; then
		echo "Usage: ibtest.sh basedir dir size peer"
		exit 1
	fi

	mountp=$1
	basedir=$2
	mb=$3
	peer=$4

	do_prepare $peer $mountp
	sleep 1
	do_ibset $peer
	sleep 1
	do_test $*
	do_cleanoff $peer $mountp
}
gfarm2()
{
	if [ $# -lt 4 ]; then
		usage
	fi

	mountp=$1
	basedir=$2
	mb=$3
	peer=$4

	sudo mkdir -p $mountp
	ssh $peer sudo mkdir -p $mountp
	gfarm2fs $mountp
	ssh $peer gfarm2fs $mountp
	sleep 1
	do_test $*
	umount $mountp
	ssh $peer umount $mountp
}
usage()
{
	echo "$prog {ibtest|dotest|gfarm2} base dir size peer"
	echo "$prog {prepare|doibset|clean} peer base"
	exit 1
}
if [ $# -eq 4 ]; then
	do_test $*
	exit $?
fi

op=$1
shift

case  $op in
mo*)
        start_mount $* ;;
umo*)
        stop_mount $* ;;
pre*)
	do_prepare $* ;;
doib*)
	do_ibset $* ;;
clean*)
	do_cleanoff $* ;;
dotest)
	do_test $* ;;
ibtest)
	ibtest $* ;;
gfarm2)
	gfarm2 $* ;;
*)
	usage $* ;;
esac
