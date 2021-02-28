#!/bin/sh

set -eu

NAME="nextcloud"
. ./_COMPOSE-COMMON.sh

${COMPOSE} $@
