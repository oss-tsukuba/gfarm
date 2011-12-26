#!/bin/sh

. ./env.sh

$PROG sync $GF_TMPF
./teardown.sh
