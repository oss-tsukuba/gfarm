#!/bin/sh

set -eu

DOCKER=$(make -s ECHO_DOCKER)
COMPOSE=$(make -s ECHO_COMPOSE)
ROOTDIR=$(make -s ECHO_ROOTDIR)
GFARM_WORKDIR=/home/user1/gfarm
SERVICES=$($COMPOSE ps --services)
GFARM_SRCDIR=$(cd $(realpath .) && cd $ROOTDIR/../.. && realpath .)

docker_cp_to() {
    FILE="$1"
    SERVICE="$2"
    SERVICE_ID="$3"
    TODIR="$4"
    $DOCKER cp "$FILE" "$SERVICE_ID":"$TODIR/$FILE"
    echo "UPDATE: $SERVICE:$TODIR/$FILE"
}

updated_files() {
    cd $GFARM_SRCDIR
    git status -s | grep -v '^??' | awk '{print $2}'
}

copy_to_service() {
    SERVICE="$1"
    FILES="$2"
    SERVICE_ID=$($COMPOSE ps -q "$SERVICE")

    cd $GFARM_SRCDIR
    for f in $FILES; do
        f=$(echo $f | sed -e "s/[\r\n]\+//g")
        docker_cp_to "$f" "$SERVICE" "$SERVICE_ID" "$GFARM_WORKDIR" &
    done
    wait
}

FILES=$(updated_files)

for s in $SERVICES; do
    copy_to_service "$s" "$FILES" &
done
wait
