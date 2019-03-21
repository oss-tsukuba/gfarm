#!/bin/sh

base=`dirname $0`
. ${base}/replica_check-common.sh
replica_check_setup_test

set_ncopy $NCOPY1 $gftmp
wait_for_rep $NCOPY1 $tmpf false "#1 increase"

set_ncopy $NCOPY2 $gftmp
wait_for_rep $NCOPY2 $tmpf false "#2 decrease"

exit_code=$exit_pass
