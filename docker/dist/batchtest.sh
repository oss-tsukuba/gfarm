#!/bin/sh
set -xeu
status=1
trap '[ $status = 1 ] && echo NG; exit $status' 0 1 2 15

test()
{
	script=$2

	DIST=$1 docker compose up -d
	docker exec gfarm-c1 sudo -u $USER \
		sh -c "(cd /home/$USER/gfarm/docker/dist && sh $script)"
	docker compose down
}

# Ubuntu
test ubuntu all.sh

for d in almalinux8 centos7
do
	for s in all.sh all-rpm.sh
	do
		test $d $s
	done
done
status=0
echo Done
