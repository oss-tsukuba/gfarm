#!/bin/sh

. ./regress.conf

$shell $testbase/umask_dir.sh 022 drwxr-xr-x
