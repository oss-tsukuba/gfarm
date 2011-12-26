#!/bin/sh

. ./env.sh

$PROG write-stat $GF_TMPF
./teardown.sh
