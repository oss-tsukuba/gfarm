#!/bin/sh

# this test may fail, if number of files which don't have enough
# replicas are too many.

. ./regress.conf
tmpf=$gftmp/foo
base=`dirname $0`
. ${base}/replica_check-common.sh

check_supported_env
backup_hostgroup
trap 'restore_hostgroup; clean_test; exit $exit_trap' $trap_sigs
clean_test
setup_test_repattr

# nlink=2
hardlink $tmpf ${tmpf}.lnk
set_repattr $NCOPY1 $gftmp
wait_for_rep $NCOPY1 $tmpf false

# directory
set_repattr $NCOPY2 $gftmp
SAVE_TIMEOUT=$NCOPY_TIMEOUT
NCOPY_TIMEOUT=10
wait_for_rep $NCOPY2 $tmpf true  ### timeout
NCOPY_TIMEOUT=$SAVE_TIMEOUT

# file
set_repattr $NCOPY2 $tmpf
wait_for_rep $NCOPY2 $tmpf false ### not timeout

restore_hostgroup
clean_test

exit $exit_code
