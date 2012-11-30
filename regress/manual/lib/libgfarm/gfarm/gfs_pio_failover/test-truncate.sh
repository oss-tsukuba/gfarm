#!/bin/sh

. ./env.sh

$PROG truncate $GF_TMPF
./teardown.sh
