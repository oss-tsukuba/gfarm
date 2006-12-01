#!/bin/sh

. ./regress.conf

# arguments are <string> [ <string>... ]
$shell $testbase/stringlist_cat.sh 1st 2nd
