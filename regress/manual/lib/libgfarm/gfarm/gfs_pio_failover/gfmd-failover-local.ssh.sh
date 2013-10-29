#!/bin/sh

: ${CONFIG_PREFIX:=}	# i.e. default is the root directory
: ${ssh:=ssh}
: ${priv:=root}

# XXX should use service_ctl() in $config_dir/config-gfarm.sysdep, instead
case `uname` in
*BSD)	: ${RC_DIR:=$CONFIG_PREFIX/etc/rc.d};;
*)	: ${RC_DIR:=$CONFIG_PREFIX/etc/init.d};;
esac


get_master()
{
	gfmdhost -l | awk '/^\+/ && $2 == "master" { print $(NF-1) }'
}

sync_slaves()
{
	gfmdhost -l |
	    awk '/^\+/ && $2 == "slave" && $3 == "sync" { print $(NF-1) }'
}

choose_sync_slave()
{
	sync_slaves | head -1
}

old_master=`get_master`
new_master=`choose_sync_slave`

echo "*** switching from $old_master to $new_master ***"

$ssh $priv@$old_master -n $RC_DIR/gfmd stop
$ssh $priv@$new_master -n 'kill -USR1 `cat '$CONFIG_PREFIX'/var/run/gfmd.pid`'

while	sleep 1
	[ X"`get_master`" != X"$new_master" ]
do
	echo >&2 "waiting for $new_master being master"
done

$ssh $priv@$old_master -n $RC_DIR/gfmd slavestart

while	sleep 1
	if sync_slaves | grep "^$old_master"'$' >/dev/null
	then false; else true; fi
do
	echo >&2 "waiting for $old_master being sync slave"
done

echo "*** switched  from $old_master to $new_master ***"

