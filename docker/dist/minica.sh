#!/bin/sh

set -eu

cd minica
[ -f minica.pem ] && exit 0

docker build . -t gfarm-minica

MINICA="docker run -u $(id -u):$(id -g) -it -w /minica -v .:/minica gfarm-minica minica"

$MINICA -domains keycloak
$MINICA -domains jwt-server
$MINICA -domains jwt-server2
