#!/bin/sh

. ./regress.conf

sh -x $testbase/umask_file.sh 000 -rw-rw-rw-
