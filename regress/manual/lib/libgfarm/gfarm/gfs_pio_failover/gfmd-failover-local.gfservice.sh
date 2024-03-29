#!/bin/sh

set -eu

GFSERVICE=${GFSERVICE-"gfservice"}
TIMEOUT=${TIMEOUT-"30"}
GFSERVICE_CONF=${GFSERVICE_CONF-"$HOME/.gfservice"}
GFSERVICE_OPT="-f $GFSERVICE_CONF"

convert_gfservice_name()
{
    hostname=$1
    egrep 'gfmd[0-9]*\=' "$GFSERVICE_CONF" |
        awk -F = '$2 == "'"$hostname"'" {print $1}'
}

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

choose_async_slave()
{
    gfmdhost -l |
        awk '$2 == "slave" && $3 == "async" { print $(NF-1) }' |
        head -1
}

get_cluster()
{
    gfmdhost -l | awk '$6 == "'$1'" { print $5 }'
}

is_synchronized()
{
    set +e
    gfmdhost -l | awk '/^\+/ && $6 == "'$1'" { print $6 }' | grep -q "$1"
    retv=$?
    set -e
    return $retv
}

wait_for_sync_one()
{
    while ! is_synchronized $1; do
        gfmdhost -l
        sleep 1
    done
    #gfmdhost -l
}

wait_for_sync_all()
{
    for gfmd in `gfmdhost`; do
        wait_for_sync_one $gfmd
    done
}

# specified gfmds are not restarted.
restart_gfmd()
{
    for gfmd in `gfmdhost`; do
        skip=0
        for ignored_gfmd in $@; do
            if [ $gfmd = $ignored_gfmd ]; then
                skip=1
                break
            fi
        done
        if [ $skip -ne 1 ]; then
            name=`convert_gfservice_name $gfmd`
            $GFSERVICE $GFSERVICE_OPT -t $TIMEOUT restart-gfmd $name
        fi
    done
}

MASTER=`get_master`
if [ -z "$MASTER" ]; then
    echo no master server
    exit 1
fi
SLAVE=`choose_async_slave`
if [ -z "$SLAVE" ]; then
    SLAVE=`choose_sync_slave`
fi
if [ -z "$SLAVE" ]; then
    echo no slave server
    gfmdhost -l
    exit 1
fi

MASTER_NAME=`convert_gfservice_name $MASTER`
SLAVE_NAME=`convert_gfservice_name $SLAVE`

MASTER_CLUSTER=`get_cluster $MASTER`
SLAVE_CLUSTER=`get_cluster $SLAVE`

### XXX If "stop-gfmd" is not executed, gfmd will freeze.
$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT stop-gfmd $SLAVE_NAME
gfmdhost -m -C ${MASTER_CLUSTER} $SLAVE
$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT start-gfmd $SLAVE_NAME
wait_for_sync_one $SLAVE

$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT stop-gfmd $MASTER_NAME

### set "metadb_server_force_slave enable" to gfmd.conf
$GFSERVICE $GFSERVICE_OPT promote-gfmd $SLAVE_NAME

$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT start-gfmd-slave $MASTER_NAME

### set async
gfmdhost -m -C ${SLAVE_CLUSTER} $MASTER

### fast synchronization
restart_gfmd $MASTER $SLAVE
wait_for_sync_all
