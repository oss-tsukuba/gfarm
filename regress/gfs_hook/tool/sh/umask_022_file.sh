#!/bin/sh

. ./regress.conf

$testbase/umask_file.sh 022 -rw-r--r--
