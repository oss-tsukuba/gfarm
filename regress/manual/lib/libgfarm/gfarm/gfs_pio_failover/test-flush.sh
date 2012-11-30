#!/bin/sh

. ./env.sh

$PROG flush $GF_TMPF
./teardown.sh
