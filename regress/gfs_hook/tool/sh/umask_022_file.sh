#!/bin/sh

. ./regress.conf

$shell $testbase/umask_file.sh 022 -rw-r--r--
