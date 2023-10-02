#!/bin/sh
set -xeu
status=1
trap '[ $status = 0 ] && echo Done || echo NG; exit $status' 0 1 2 15

DOCKEREXEC="docker exec -u $USER -w /home/$USER/gfarm/docker/dist gfarm-c1"

test()
{
	script="$2"
	JWT=false
	[ $# -gt 2 ] && JWT=true

	DIST=$1 docker compose build --build-arg UID=$(id -u) c1
	DIST=$1 docker compose up -d

	# JWT-Server
	$JWT && (cd jwt-server && docker compose up -d && make setup)

	# execute a script
	$DOCKEREXEC sh $script -min

	# SASL XOAUTH2 test
	$JWT && $DOCKEREXEC sh ./check-oauth.sh

	docker compose down
}

# clean up
make down

## Ubuntu
test ubuntu all.sh jwt

# AlmaLinux8 and CentOS7
for d in almalinux8 centos7
do
	for s in all.sh "all.sh -pkg"
	do
		test $d "$s"
	done
done

status=0
