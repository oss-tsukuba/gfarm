#!/bin/sh
set -xeu

TERM_INHERIT=${TERM+--env TERM=${TERM}}

docker build -f Dockerfile ../.. -t alma8
docker run -d -v /sys/fs/cgroup:/sys/fs/cgroup:ro --cap-add=SYS_ADMIN \
	--device=/dev/fuse --name alma8 alma8
docker exec ${TERM_INHERIT} -w /root -it alma8 /bin/bash

# sh gfarm/docker/dist/mkrpm.sh
# Ctrl-D

# docker cp alma8:/root/rpmbuild/SRPMS/gfarm-$VER-1.src.rpm .
# docker stop alma8
# docker rm alma8
