#!/bin/sh

. ./regress.conf

$shell $testbase/umask_dir.sh 077 drwx------
