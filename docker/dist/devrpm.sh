#!/bin/sh
set -xeu

docker build -f Dockerfile-AlmaLinux8-single ../.. -t alma8
docker run -d -v /sys/fs/cgroup:/sys/fs/cgroup:ro --cap-add=SYS_ADMIN \
	--device=/dev/fuse --name alma8 alma8
docker exec -w /root -it alma8 /bin/bash

# sh gfarm/docker/dist/mkrpm.sh
# Ctrl-D

# docker cp alma8:/root/rpmbuild/SRPMS/gfarm-$VER-1.src.rpm .
# docker stop alma8
# docker rm alma8
