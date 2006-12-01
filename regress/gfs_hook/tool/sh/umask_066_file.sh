#!/bin/sh

. ./regress.conf

$shell $testbase/umask_file.sh 066 -rw-------
