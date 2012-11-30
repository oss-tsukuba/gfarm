#!/bin/sh

. ./env.sh

$PROG datasync $GF_TMPF
./teardown.sh
