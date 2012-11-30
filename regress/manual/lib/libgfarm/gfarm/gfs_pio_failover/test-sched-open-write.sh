#!/bin/sh

. ./env.sh

$PROG sched-open-write $GF_TMPF
./teardown.sh
