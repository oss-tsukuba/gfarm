#!/bin/sh

: ${CONFIG_PREFIX:=}	# i.e. default is the root directory
: ${ssh:=ssh}
: ${priv:=root}

# XXX this assumes that the path of gfjournal is same on the remote host
: ${gfjournal:=`type gfjournal | awk '{print $NF}'`}

# XXX should use service_ctl() in $config_dir/config-gfarm.sysdep, instead
case `uname` in
*BSD)	: ${RC_DIR:=$CONFIG_PREFIX/etc/rc.d};;
*)	: ${RC_DIR:=$CONFIG_PREFIX/etc/init.d};;
esac


get_master()
{
	gfmdhost -l | awk '/^\+/ && $2 == "master" { print $(NF-1) }'
}

choose_sync_slave()
{
	gfmdhost -l |
	    awk '/^\+/ && $2 == "slave" && $3 == "sync" { print $(NF-1) }' |
	    head -1
}

# NOTE: We cannot guarantee async slaves not to stop

sync_slaves_are_not_ready()
{
	gfmdhost -l |
		awk '$2 == "slave" && $3 == "sync" { print $1, $(NF-1) }' |
		grep '^[^+]' >/dev/null
}

sync_slave_stopped()
{
	gfmdhost -l |
		awk '$2 == "slave" && $3 == "sync" { print $1, $(NF-1) }' |
		grep '^[^-+|?]' >/dev/null
}

get_seqnum()
{
	$ssh $1 -n $gfjournal -m $CONFIG_PREFIX/var/gfarm-metadata/journal/0000000000.gmj
}

while
	while	old_master=`get_master`
		new_master=`choose_sync_slave`
		[ -z "$old_master" -o -z "$new_master" ]
	do
		echo "--- determining current/next master ($old_master/$new_master) ---"
		sleep 1
		if sync_slave_stopped; then
			echo >&2 "!!! error happened !!!"
			gfmdhost -l
			exit 1
		fi
	done

	echo "--- stopping old master ($old_master -> $new_master) ---"

	$ssh $priv@$old_master -n $RC_DIR/gfmd stop

	# there may be one sequence number difference here
	old_seqnum=`get_seqnum $priv@$old_master`
	new_seqnum=`get_seqnum $priv@$new_master`
	[ X"$new_seqnum" != X"$old_seqnum" ]
do
	echo "--- seqnum mismatch $new_seqnum vs $old_seqnum, restarting old master for sync ($old_master -> $new_master) ---"
	$ssh $priv@$old_master -n $RC_DIR/gfmd start
done

echo "*** switching from $old_master to $new_master ***"

$ssh $priv@$new_master -n 'kill -USR1 `cat '$CONFIG_PREFIX'/var/run/gfmd.pid`'

while	current=`get_master`
	[ X"$current" != X"$new_master" ]
do
	echo "--- waiting for new master, current=$current ($old_master -> $new_master) ---"
	sleep 1
done

$ssh $priv@$old_master -n $RC_DIR/gfmd slavestart

while	sync_slaves_are_not_ready
do
	if sync_slave_stopped; then
		echo >&2 "!!! error happened !!!"
		gfmdhost -l
		exit 1
	fi
	echo "--- waiting for sync slaves ($old_master -> $new_master) ---"
	sleep 1
done

echo "*** switched  from $old_master to $new_master ***"

exit 0
