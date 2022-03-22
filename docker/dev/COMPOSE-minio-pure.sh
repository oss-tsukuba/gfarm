#!/bin/sh

set -eu

NAME="minio-pure"
. ./_COMPOSE-COMMON.sh

${COMPOSE} $@
