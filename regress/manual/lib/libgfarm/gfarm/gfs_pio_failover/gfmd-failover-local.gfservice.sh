#!/bin/sh
GFSERVICE=${GFSERVICE-"gfservice"}
TIMEOUT=${TIMEOUT-"30"}
BACKUP_FILE=gfarm-pgsql.dmp

if [ X$GFSERVICE_CONF != X ]; then
	GFSERVICE_OPT="-f $GFSERVICE_CONF"
fi

MASTER=`cat master`
SLAVE=`cat slave`
if [ X$MASTER = X ]; then
        MASTER=gfmd1
        SLAVE=gfmd2
fi

$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT stop-gfmd $MASTER
$GFSERVICE $GFSERVICE_OPT promote-gfmd $SLAVE
$GFSERVICE $GFSERVICE_OPT backup-backend-db $SLAVE > $BACKUP_FILE
$GFSERVICE $GFSERVICE_OPT restore-backend-db $MASTER < $BACKUP_FILE
$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT stop-gfmd gfmd3
$GFSERVICE $GFSERVICE_OPT restore-backend-db gfmd3 < $BACKUP_FILE
$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT start-gfmd-slave $MASTER
$GFSERVICE $GFSERVICE_OPT -t $TIMEOUT start-gfmd gfmd3

rm -f $BACKUP_FILE

echo $MASTER > slave
echo $SLAVE > master
