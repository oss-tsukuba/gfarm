#!/bin/sh
set -xeu
status=1
trap '[ $status = 0 ] && echo Done || echo NG; exit $status' 0 1 2 15

test()
{
	script=$2

	DIST=$1 docker compose build c1
	DIST=$1 docker compose up -d
	docker exec -u $USER -w /home/$USER/gfarm/docker/dist gfarm-c1 \
		sh $script
	docker compose down
}

# clean up
docker compose down

# Ubuntu
test ubuntu all.sh

# AlmaLinux8 and CentOS7
for d in almalinux8 centos7
do
	for s in all.sh all-rpm.sh
	do
		test $d $s
	done
done
status=0
