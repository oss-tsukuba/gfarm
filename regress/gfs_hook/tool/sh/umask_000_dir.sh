#!/bin/sh

. ./regress.conf

$shell $testbase/umask_dir.sh 000 drwxrwxrwx
