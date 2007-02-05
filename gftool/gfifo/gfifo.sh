#!/bin/sh
#
# gfifo.sh script submits processes on a specified host when it
# is available.  -G or -N option specifies the total number of
# processes.  -H option specifies the host list that limits the
# number of parallel executions.
#
# $Id$

FIFO=/tmp/gfifo-$$

error()
{
	echo >&2 $*
	usage
}

usage()
{
	echo >&2 "usage: gfifo.sh [ -G file | -N #proc ] [ -H hostfile ]"
	echo >&2 "                [ -o stdout ] [ -e stderr ] [ -prb ] ..."
	exit 1
}

# setup host list
setup_hostlist()
{
	if [ X$HOSTFILE = X ]; then
		gfhost > $FIFO &
	else
		cat $HOSTFILE > $FIFO &
	fi
}

job()
{
	gfrcmd -n $1 gfexec $GFRCMD_ARGS --gfarm_index $2 $PROGS >&2
}

submit()
{
	i=0
	cat $FIFO | while read h
	do
		(job $h $i; echo $h) &
		i=`expr $i + 1`
		[ $i -ge $NUM_PROC ] && {
			wait
			break
		}
	done > $FIFO
}

while [ $# -gt 0 ]; do
    case $1 in
	-G) shift; [ $# -ge 1 ] || usage
	    [ X$NUM_PROC = X ] || error -G option cannot be specified with -N
	    SCHEDFILE=$1
	    ;;
	-N) shift; [ $# -ge 1 ] || usage
	    [ X$SCHEDFILE = X ] || error -N option cannot be specified with -G
	    NUM_PROC=$1
	    ;;
	-H) shift; [ $# -ge 1 ] || usage
	    HOSTFILE=$1
	    [ -e $HOSTFILE ] || error $HOSTFILE: not exist
	    ;;
	-o) shift; [ $# -ge 1 ] || usage
	    STDOUT=$1
	    ;;
	-e) shift; [ $# -ge 1 ] || usage
	    STDERR=$1
	    ;;
	-p) PROFILE=yes
	    ;;
	-r) REPLICATION=yes
	    ;;
	-b) GLOBAL=yes
	    ;;
	*) break
	    ;;
    esac
    shift
done

[ X$SCHEDFILE = X -a X$NUM_PROC = X ] &&
	error either -G or -N option should be specified
if [ X$SCHEDFILE != X ]; then
    NUM_PROC=$(gfwhere $SCHEDFILE | wc -l)
    NUM_PROC=`expr $NUM_PROC - 1`
fi
GFRCMD_ARGS="--gfarm_nfrags $NUM_PROC"
[ X$STDOUT = X ] || GFRCMD_ARGS="$GFRCMD_ARGS --gfarm_stdout=$STDOUT"
[ X$STDERR = X ] || GFRCMD_ARGS="$GFRCMD_ARGS --gfarm_stderr=$STDERR"
[ X$PROFILE = X ] || GFRCMD_ARGS="$GFRCMD_ARGS --gfarm_profile"
[ X$REPLICATION = X ] || GFRCMD_ARGS="$GFRCMD_ARGS --gfarm_replicate"
[ X$GLOBAL = X ] || GFRCMD_ARGS="$GFRCMD_ARGS --gfarm_hook_global"
[ X$GFS_PWD = X ] || GFRCMD_ARGS="$GFRCMD_ARGS --gfarm_cwd $GFS_PWD"

PROGS="$*"

mknod $FIFO p

setup_hostlist 
submit

rm -f $FIFO
