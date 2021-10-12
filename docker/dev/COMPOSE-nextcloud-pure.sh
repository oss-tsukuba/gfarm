#!/bin/sh

set -eu

NAME="nextcloud-pure"
. ./_COMPOSE-COMMON.sh

${COMPOSE} $@
