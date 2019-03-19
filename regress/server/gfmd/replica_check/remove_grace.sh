#!/bin/sh

LONG_TEST=0
GRACE_TIME_DIFF=7  # sec.

GFPREP_OPT=''

base=`dirname $0`
. ${base}/replica_check-common.sh
replica_check_setup_test
# $tmpf was created
ATIME=`date +%s`  # XXX - should update atime just before wait_for_rep

set_ncopy $NCOPY2 $gftmp  ### ncopy=2
wait_for_rep $NCOPY2 $tmpf false "#0"  ### not timeout

set_grace_used_space_ratio 0

GRACE_TIME=`expr $NCOPY_TIMEOUT + $GRACE_TIME_DIFF`
set_grace_time $GRACE_TIME

gfprep_n $NCOPY1 $tmpf  ### increase replicas: 2 -> 3

echo -n "before test#1 replicas: "
gfwhere ${tmpf}

# XXX - update atime of $tmpf here
echo "test #1: A surplus replica is not removed."
echo "         (timeout is expected) ($NCOPY_TIMEOUT sec.)"
wait_for_rep $NCOPY2 $tmpf true  "#1"  ### timeout

NOW=`date +%s`
sleep_time=`expr $GRACE_TIME - $NOW + $ATIME + 1`
if [ $sleep_time -ge 0 ]; then
  echo "sleep($sleep_time) for grace_time"
  sleep $sleep_time
fi
### reach grace time

echo "test #2: A surplus replica is removed." ### decrease replicas: 3 -> 2
wait_for_rep $NCOPY2 $tmpf false "#2"  ### not timeout

### short test
if [ $LONG_TEST -eq 0 ]; then
  exit_code=$exit_pass
  exit
fi

##########################################################################
echo "test #3: disable replica_check_grace_* (remove all surplus replicas)"
set_grace_used_space_ratio 0
set_grace_time 0
gfprep_n $NCOPY1 $tmpf  ### increase replicas: 2 -> 3
wait_for_rep $NCOPY2 $tmpf false "#3"  ### not timeout

echo "test #4: replica_check_grace_used_space_ratio=100"
echo "         (not remove surplus replicas)"
echo "         (timeout is expected) ($NCOPY_TIMEOUT sec.)"
set_grace_used_space_ratio 100
set_grace_time 0
gfprep_n $NCOPY1 $tmpf  ### increase replicas: 2 -> 3
wait_for_rep $NCOPY2 $tmpf true "#4"  ### timeout

exit_code=$exit_pass
