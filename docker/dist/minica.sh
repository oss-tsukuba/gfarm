#!/bin/sh

set -eu

UPDATE="${1:-}"

HOSTS="keycloak jwt-server jwt-server2"

cd minica

remove_key() {
	HOST=$1
	rm -f $HOST/*.pem
}

if [ "$UPDATE" = "--update" ]; then
	rm -f minica.pem minica-key.pem
	for h in $HOSTS; do
		remove_key $h
	done
fi

[ -f minica.pem ] && exit 0

docker build . -t gfarm-minica

MINICA="docker run --rm -u $(id -u):$(id -g) -w /minica -v .:/minica gfarm-minica"

create_key() {
	HOST=$1
	$MINICA -domains $HOST
	chmod go+r *.pem
	chmod go+rx $HOST
	chmod go+r $HOST/*.pem
}

for h in $HOSTS; do
	create_key $h
done
