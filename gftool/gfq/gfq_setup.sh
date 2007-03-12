#! /bin/sh
#
# $Id$

usage () {
	echo usage: $0 file cmd ...
	exit 1
}

error() {
	echo $*
	exit 1
}

file=$1
[ X$file = X ] && usage
gftest -f $1 || error "$1: no such file"

[ X$GFQ_DIR = X ] && GFQ_DIR=/tmp/.gfq-$USER
[ ! -d $GFQ_DIR ] && mkdir $GFQ_DIR
[ ! -d $GFQ_DIR ] && error "$GFQ_DIR: cannot create a temporary directory"

shift
cmd=$*

n=$(gfwhere $file | wc -l)
n=`expr $n - 1`
i=0
gfsched $file | while read host
do
	echo gfexec -I $i -N $n $cmd >> $GFQ_DIR/$host
	i=`expr $i + 1`
done
