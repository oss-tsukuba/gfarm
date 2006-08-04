#!/bin/sh

. ./regress.conf

$testbase/umask_dir.sh 000 drwxrwxrwx
