#!/bin/sh

set -eu

cd minica
[ -f minica.pem ] && exit 0

docker build . -t gfarm-minica

MINICA="docker run -u $(id -u):$(id -g) -w /minica -v .:/minica gfarm-minica minica"

create_key() {
	HOST=$1
	$MINICA -domains $HOST
	chmod go+r *.pem
	chmod go+rx $HOST
	chmod go+r $HOST/*.pem
}

create_key keycloak
create_key jwt-server
create_key jwt-server2
