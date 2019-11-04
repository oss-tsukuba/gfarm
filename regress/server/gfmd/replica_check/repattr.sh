#!/bin/sh

# this test may fail, if number of files which don't have enough
# replicas are too many.

base=`dirname $0`
. ${base}/replica_check-common.sh
setup_test

set_repattr $NCOPY1 $gftmp
# wait replica_check_minimum_interval
wait_for_rep $NCOPY1 $tmpf false  "#1 increase"

set_repattr $NCOPY2 $gftmp
# wait replica_check_minimum_interval
wait_for_rep $NCOPY2 $tmpf false "#2 decrease"

clean_test
exit $exit_code
