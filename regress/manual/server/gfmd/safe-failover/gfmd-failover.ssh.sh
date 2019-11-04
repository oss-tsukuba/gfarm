#!/bin/sh

: ${CONFIG_PREFIX:=}	# i.e. default is the root directory
: ${ssh:=ssh}
: ${priv:=root}
: ${timeout:=30}

# XXX this assumes that the path of gfjournal is same on the remote host
: ${gfjournal:=`type gfjournal | awk '{print $NF}'`}

# XXX should use service_ctl() in $config_dir/config-gfarm.sysdep, instead
case `uname` in
*BSD)	: ${RC_DIR:=$CONFIG_PREFIX/etc/rc.d};;
*)	: ${RC_DIR:=$CONFIG_PREFIX/etc/init.d};;
esac

status=1

# use this file as cache of "gfmdhost -l"
gfmdlist=/tmp/gfmdhost$$
trap 'rm -f $gfmdlist; exit $status' 0 1 2 15

# this function caches "gfmdhost -l" to $gfmdhost as well
gfmdhosts_down()
{
	if gfmdhost -l >$gfmdlist; then
		if grep '^[^?|+]' $gfmdlist >/dev/null; then
			awk '/^[^?|+]/ { printf "%s ", $(NF-1) }' $gfmdlist
			return 0
		fi
		return 1
	fi
	echo >&2 "!!! cannot run gfmdhost command"
	exit
}

get_master()
{
	awk '$2 == "master" { print $(NF-1) }' $gfmdlist
}

choose_sync_slave()
{
	awk '$2 == "slave" && $3 == "sync" { print $(NF-1); exit }' $gfmdlist
}

get_slaves()
{
	awk '$2 == "slave" { print $(NF-1) }' $gfmdlist
}

get_seqnum()
{
	$ssh $1 -n $gfjournal -m $CONFIG_PREFIX/var/gfarm-metadata/journal/0000000000.gmj
}

elapsed=0
while	down_hosts=`gfmdhosts_down`
do
	echo "--- waiting for ${down_hosts}to become up ---"
	sleep 1
	elapsed=`expr $elapsed + 1`
	if [ $elapsed -gt $timeout ]; then
		echo >&2 "!!! timeout waiting for $down_hosts to become up !!!"
		gfmdhost -l
		exit
	fi
done

# choose new master
old_master=`get_master`
new_master=`choose_sync_slave`
if [ -z "$old_master" -o -z "$new_master" ]; then
	echo >&2 "!!! cannot choose new master. old:$old_master new:$new_master !!!"
	cat $gfmdlist
	exit
fi

echo "--- stopping old master ($old_master -> $new_master) ---"

# make new journal records.
awk 'BEGIN{for(i=0;i<100;i++)print i;exit}' |
while read line; do gfmkdir /tmp/fotest; gfrmdir /tmp/fotest; done 2>/dev/null

# start failover
$ssh $priv@$old_master -n $RC_DIR/gfmd stop

echo "--- checking sequence number ---"

# make sure that sequence numbers match
old_seqnum=`get_seqnum $priv@$old_master`
while read stat master_slave type mode cluster host port; do
	if [ X"$master_slave" = X"slave" ]; then
		new_seqnum=`get_seqnum $priv@$host`
		if [ X"$new_seqnum" != X"$old_seqnum" ]; then
			echo >&2 "!!! seqnum mismatch $new_seqnum@$host vs $old_seqnum@old_master !!!"
			exit
		fi
	fi
done <$gfmdlist

echo "*** switching from $old_master to $new_master ***"
$ssh $priv@$new_master -n 'kill -USR1 `cat '$CONFIG_PREFIX'/var/run/gfmd.pid`'
$ssh $priv@$old_master -n $RC_DIR/gfmd slavestart

echo "*** switched  from $old_master to $new_master ***"

status=0
exit
