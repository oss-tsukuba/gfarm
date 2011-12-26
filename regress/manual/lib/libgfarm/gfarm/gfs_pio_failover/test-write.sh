#!/bin/sh

. ./env.sh

$PROG write $GF_TMPF
./teardown.sh
