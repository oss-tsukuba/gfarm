#!/bin/sh

. ./regress.conf

$shell $testbase/umask_file.sh 000 -rw-rw-rw-
