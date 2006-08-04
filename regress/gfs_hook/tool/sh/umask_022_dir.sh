#!/bin/sh

. ./regress.conf

$testbase/umask_dir.sh 022 drwxr-xr-x
