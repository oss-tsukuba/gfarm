#!/bin/sh

# this test may fail, if number of files which don't have enough
# replicas are too many.

base=`dirname $0`
. ${base}/replica_check-common.sh
setup_test

# setup: create a nlink=2 file
hardlink $tmpf ${tmpf}.lnk
set_ncopy $NCOPY1 $gftmp
# wait replica_check_minimum_interval
wait_for_rep $NCOPY1 $tmpf false "#1 setup nlink=2"  ### not timeout

# set ncopy to the parent dirctory: do not decrease replicas
set_ncopy $NCOPY2 $gftmp
wait_for_rep $NCOPY2 $tmpf true "#2 parent dir"  ### timeout

# set ncopy itself: can decrease replicas
set_ncopy $NCOPY2 $tmpf
# wait replica_check_minimum_interval
wait_for_rep $NCOPY2 $tmpf false "#3 file" ### not timeout


clean_test
exit $exit_code
