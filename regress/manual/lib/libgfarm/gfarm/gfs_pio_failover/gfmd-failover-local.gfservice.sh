#!/bin/sh

set -eu

GFSERVICE=${GFSERVICE-"gfservice"}
TIMEOUT=${TIMEOUT-"30"}
GFSERVICE_CONF=${GFSERVICE_CONF-""}

if [ X$GFSERVICE_CONF != X ]; then
    GFSERVICE_OPT="-f $GFSERVICE_CONF"
else
    GFSERVICE_OPT=""
fi

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
            $GFSERVICE $GFSERVICE_OPT -t $TIMEOUT restart-gfmd $gfmd
        fi
    done
}

MASTER=`get_master`
if [ -z "$MASTER" ]; then
    exit 1
fi
SLAVE=`choose_async_slave`
if [ -z "$SLAVE" ]; then
    SLAVE=`choose_sync_slave`
fi

MASTER_CLUSTER=`get_cluster $MASTER`
SLAVE_CLUSTER=`get_cluster $SLAVE`

### XXX If "stop-gfmd" is not executed, gfmd will freeze.
$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT stop-gfmd $SLAVE
gfmdhost -m -C ${MASTER_CLUSTER} $SLAVE
$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT start-gfmd $SLAVE
wait_for_sync_one $SLAVE

$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT stop-gfmd $MASTER

### set "metadb_server_force_slave enable" to gfmd.conf
$GFSERVICE $GFSERVICE_OPT promote-gfmd $SLAVE

$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT start-gfmd-slave $MASTER

### set async
gfmdhost -m -C ${SLAVE_CLUSTER} $MASTER

### fast synchronization
restart_gfmd $MASTER $SLAVE
wait_for_sync_all
