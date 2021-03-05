#!/bin/sh

set -eu

NAME="minio"
. ./_COMPOSE-COMMON.sh

${COMPOSE} $@
