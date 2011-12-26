#!/bin/sh

. ./env.sh

$PROG sched-create-write $GF_TMPF
./teardown.sh
