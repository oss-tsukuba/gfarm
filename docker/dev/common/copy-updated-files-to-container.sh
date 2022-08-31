#!/bin/bash

set -eu
#set -x

BASEDIR="$PWD"
#DOCKER=$(make -s ECHO_DOCKER)
COMPOSE=$(make -s ECHO_COMPOSE)
ROOTDIR=$(make -s ECHO_ROOTDIR)
SERVICES=$(eval $COMPOSE ps --services)
GFARM_SRCDIR=$(cd $(realpath .) && cd $ROOTDIR/../.. && realpath .)
GFARM2FS_SRCDIR="${GFARM_SRCDIR}/gfarm2fs"

USERNAME="user1"
HOMEDIR="/home/${USERNAME}"
GFARM_WORKDIR="${HOMEDIR}/gfarm"
GFARM2FS_WORKDIR="${HOMEDIR}/gfarm2fs"
SCP_HOST="gfclient1"  # in .ssh/config

updated_files() {
    git status -s | grep -v '^??' | awk '{print $2}'
}

scp0() {
    SRC="$1"
    DST="$2"
    scp -q "$SRC" "$DST"
    echo "COPY TO: $DST"
}

scp_to_container() {
    HOST="$1"

    cd "$GFARM_SRCDIR"
    for f in $(updated_files); do
        f=$(echo $f | sed -e "s/[\r\n]\+//g")
        scp0 "$f" "$HOST:$GFARM_WORKDIR/$f" &
    done
    cd "$GFARM2FS_SRCDIR"
    for f in $(updated_files); do
        f=$(echo $f | sed -e "s/[\r\n]\+//g")
        scp0 "$f" "$HOST:$GFARM2FS_WORKDIR/$f" &
    done
    wait
}

rsync_fromto() {
    FROM="$1"
    TO="$2"

    cd "$BASEDIR"
    eval $COMPOSE exec -T -u "$USERNAME" "$FROM" bash -c \"rsync -a ${GFARM_WORKDIR}/ ${TO}:${GFARM_WORKDIR}/\"
    eval $COMPOSE exec -T -u "$USERNAME" "$FROM" bash -c \"rsync -a ${GFARM2FS_WORKDIR}/ ${TO}:${GFARM2FS_WORKDIR}/\"
    echo "synchronized: $TO"
}

scp_to_container "$SCP_HOST"

for s in $SERVICES; do
    [ $s = "client1" ] && continue
    rsync_fromto client1 $s &
done
wait
