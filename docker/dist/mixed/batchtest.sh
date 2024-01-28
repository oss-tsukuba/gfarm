#!/bin/sh
set -xeu
status=1
trap '[ $status = 0 ] && echo Done || echo NG; exit $status' 0 1 2 15

DOCKEREXEC="docker exec -u $USER -w /home/$USER/gfarm/docker/dist/mixed gfarm-c1"

# clean up
make down

JWT=true

docker compose up -d

# JWT-Server
$JWT && (cd ../jwt-server && docker compose up -d && make setup)

# execute a script
$DOCKEREXEC sh all.sh

# SASL XOAUTH2 test
$JWT && $DOCKEREXEC sh ../check-oauth.sh

# clean up
make down

status=0
