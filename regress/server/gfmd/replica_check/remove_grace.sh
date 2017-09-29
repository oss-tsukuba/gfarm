#!/bin/sh

# this test may fail, if number of files which don't have enough
# replicas are too many.

LONG_TEST=0

NCOPY_TIMEOUT_EXPECT=15  # sec.
GFPREP_OPT='-d -B'

base=`dirname $0`
. ${base}/replica_check-common.sh
setup_test


set_ncopy $NCOPY2 $gftmp  ### ncopy=2

set_grace_used_space_ratio 0
set_grace_time `expr $NCOPY_TIMEOUT_EXPECT + 1`

gfprep_n $NCOPY1 $tmpf  ### increase replicas: 2 -> 3

echo -n "before test#1 replicas: "
gfwhere ${tmpf}

echo "test #1: A surplus replica is not removed."
echo "         (timeout is expected) ($NCOPY_TIMEOUT_EXPECT sec.)"
SAVE_TIMEOUT=$NCOPY_TIMEOUT
NCOPY_TIMEOUT=$NCOPY_TIMEOUT_EXPECT
wait_for_rep $NCOPY2 $tmpf true  "#1"  ### timeout
NCOPY_TIMEOUT=$SAVE_TIMEOUT

sleep 1 ### reach grace time

echo "test #2: A surplus replica is removed." ### decrease replicas: 3 -> 2
# wait replica_check_minimum_interval
wait_for_rep $NCOPY2 $tmpf false "#2"  ### not timeout

### short test
if [ $LONG_TEST -eq 0 ]; then
  clean_test  ### include restore_grace
  exit $exit_code
fi

echo "test #3: disable replica_check_grace_* (remove all surplus replicas)"
set_grace_used_space_ratio 0
set_grace_time 0
gfprep_n $NCOPY1 $tmpf  ### increase replicas: 2 -> 3
# wait replica_check_minimum_interval
wait_for_rep $NCOPY2 $tmpf false "#3"  ### not timeout

echo "test #4: replica_check_grace_used_space_ratio=100"
echo "         (not remove surplus replicas)"
echo "         (timeout is expected) ($NCOPY_TIMEOUT_EXPECT sec.)"
set_grace_used_space_ratio 100
set_grace_time 0
gfprep_n $NCOPY1 $tmpf  ### increase replicas: 2 -> 3
SAVE_TIMEOUT=$NCOPY_TIMEOUT
NCOPY_TIMEOUT=$NCOPY_TIMEOUT_EXPECT
wait_for_rep $NCOPY2 $tmpf true "#4"  ### timeout
NCOPY_TIMEOUT=$SAVE_TIMEOUT


clean_test  ### include restore_grace
exit $exit_code
