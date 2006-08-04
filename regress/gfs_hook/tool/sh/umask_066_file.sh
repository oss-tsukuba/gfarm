#!/bin/sh

. ./regress.conf

$testbase/umask_file.sh 066 -rw-------
