#!/bin/bash

# Required
# $ podman pull docker.io/docker/compose:1.29.2

# for rootless podman
#API_SOCK=/run/user/${UID}/podman/podman.sock

# for rootful podman
API_SOCK=/run/podman/podman.sock

# for rootless docker
#API_SOCK=/run/user/${UID}/docker.sock

# for rootful docker
#API_SOCK=/run/docker.sock

SCRIPTS_DIR=$(dirname $(realpath $0))
TOP=${SCRIPTS_DIR}/../../..
podman run -it --rm \
       -e COMPOSE_PROJECT_NAME=${COMPOSE_PROJECT_NAME} \
       -e GFDOCKER_PRJ_NAME=${GFDOCKER_PRJ_NAME} \
       -v ${API_SOCK}:/var/run/docker.sock:z \
       -v "${TOP}:${TOP}:ro" \
       -w "/${PWD}" \
       docker/compose:1.29.2 "$@"
