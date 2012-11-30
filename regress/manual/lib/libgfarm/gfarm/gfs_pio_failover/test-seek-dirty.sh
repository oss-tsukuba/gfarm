#!/bin/sh

. ./env.sh

$PROG seek-dirty $GF_TMPF
./teardown.sh
