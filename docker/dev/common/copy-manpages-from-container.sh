#!/bin/sh

set -eu

DOCKER=$(make -s ECHO_DOCKER)
COMPOSE=$(make -s ECHO_COMPOSE)
ROOTDIR=$(make -s ECHO_ROOTDIR)
GFARM_WORKDIR=/home/user1/gfarm
GFARM_SRCDIR=$(cd $(realpath .) && cd $ROOTDIR/../.. && realpath .)
COMPOSE_EXEC="$COMPOSE exec -T client1 bash -c"

docker_cp_from() {
    FILE="$1"
    SERVICE="$2"
    SERVICE_ID="$3"
    FROMDIR="$4"

    $DOCKER cp "$SERVICE_ID":"$FROMDIR/$FILE" "$FILE"
    echo "GET FROM: $SERVICE:$FROMDIR/$FILE"
}

copy_from_service() {
    SERVICE="$1"
    FILES="$2"
    SERVICE_ID=$($COMPOSE ps -q "$SERVICE")

    cd $GFARM_SRCDIR
    for f in $FILES; do
        f=$(echo $f | sed -e "s/[\r\n]\+//g")
        docker_cp_from "$f" "$SERVICE" "$SERVICE_ID" "$GFARM_WORKDIR"
    done
}

#$COMPOSE_EXEC "xsltproc --version"
$COMPOSE_EXEC "cd $GFARM_WORKDIR && make man html" > /dev/null 2>&1

FILES=$($COMPOSE_EXEC "cd $GFARM_WORKDIR && git status -s man/ doc/html/" | grep -v '^??' | awk '{print $2}')

copy_from_service "client1" "$FILES"
