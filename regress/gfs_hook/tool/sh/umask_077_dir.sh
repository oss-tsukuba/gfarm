#!/bin/sh

. ./regress.conf

$testbase/umask_dir.sh 077 drwx------
