#!/bin/sh
set -xeu
status=1
trap '[ $status = 0 ] && echo Done || echo NG; exit $status' 0 1 2 15

option=min
TEST_OPTION=
DEBIAN=
RHEL=
DIST_SPECIFIED=false
while [ $# -gt 0 ]
do
	case $1 in
	regress) option=$1 ;;
	jwt) TEST_OPTION=$1 ;;
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
	JWT=false
	[ $# -gt 3 ] && JWT=true

	DIST=$1 docker compose build --build-arg UID=$(id -u) c1
	DIST=$1 docker compose up -d

	# JWT-Server
	$JWT && (cd jwt-server && docker compose up -d && make setup)

	# execute a script
	$DOCKEREXEC sh $script $opt

	# SASL XOAUTH2 test
	$JWT && $DOCKEREXEC sh ./check-oauth.sh $opt

	docker compose down
}

# debian
for d in $DEBIAN
do
	[ X"$d" = XNONE ] && break
	test $d all.sh "$option" $TEST_OPTION
	[ X"$TEST_OPTION" = X ] || TEST_OPTION=
done

# RHEL
for d in $RHEL
do
	[ X"$d" = XNONE ] && break
	for s in all.sh "all.sh pkg"
	do
		test $d "$s" "$option" $TEST_OPTION
		[ X"$TEST_OPTION" = X ] || TEST_OPTION=
	done
done

# clean up
make down

status=0
