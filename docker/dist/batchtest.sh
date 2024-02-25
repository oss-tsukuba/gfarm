#!/bin/sh
set -xeu
status=1
trap '[ $status = 0 ] && echo Done || echo NG; exit $status' 0 1 2 15

option=min
DEBIAN=
RHEL=
DIST_SPECIFIED=false
while [ $# -gt 0 ]
do
	case $1 in
	regress|regress_full) option=$1 ;;
	ubuntu) DEBIAN="$DEBIAN $1"; DIST_SPECIFIED=true ;;
	rockylinux9|almalinux8|centos7) RHEL="$RHEL $1"; DIST_SPECIFIED=true ;;
	*) exit 1 ;;
	esac
	shift
done

$DIST_SPECIFIED || {
	DEBIAN=ubuntu
	RHEL="rockylinux9 almalinux8 centos7"
}
[ X"$DEBIAN" = X ] && DEBIAN=NONE
[ X"$RHEL" = X ] && RHEL=NONE

DOCKEREXEC="docker exec -u $USER -w /home/$USER/gfarm/docker/dist gfarm-c1"

test()
{
	script="$2"
	opt="$3"

	DIST=$1 docker compose build --build-arg UID=$(id -u) c1
	DIST=$1 docker compose up -d

	# execute a script
	$DOCKEREXEC sh $script $opt

	# SASL XOAUTH2 test
	$DOCKEREXEC sh ./check-oauth.sh $opt

	# Multitenant test
	$DOCKEREXEC sh ./check-multitenant.sh $opt

	docker compose down
}

# clean up
make down

# JWT Server
docker compose up -d	# for gfarm_net
(cd jwt-server && docker compose up -d && make setup)
docker compose down

# debian
for d in $DEBIAN
do
	[ X"$d" = XNONE ] && break
	test $d all.sh "$option"
done

# RHEL
for d in $RHEL
do
	[ X"$d" = XNONE ] && break
	test $d all.sh "$option"
	test $d all.sh "pkg $option"
done

# clean up
make down

status=0
