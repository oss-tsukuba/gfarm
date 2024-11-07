#!/bin/sh

docker build . -t gfarm-minica

MINICA="docker run -u $(id -u):$(id -g) -it -w /minica -v .:/minica gfarm-minica minica"

$MINICA -domains keycloak
$MINICA -domains jwt-server
$MINICA -domains jwt-server2
