#!/bin/sh

base=$(dirname $0)
. ${base}/readonly-common.sh
. ${base}/../replica_check/replica_check-common.sh
replica_check_setup_test

trap 'clean_test; replica_check_clean_test; exit $exit_trap' $trap_sigs
trap 'clean_test; replica_check_clean_test; exit $exit_code' 0


set_ncopy $NCOPY1 $gftmp
wait_for_rep $NCOPY1 $tmpf false "#1 increase"

hosts="$(gfwhere $tmpf | tr ' ' '\n')"
rohost="$(echo "$hosts" | head -n 1)"  # to set readonly
flags="$(query_host_flags "$rohost")"
if [ "$?" -ne 0 ] || [ "X${flags}" = X ]; then
    echo "failed: $0"
    exit
fi

gfhost -m -f "$(set_readonly_flag "$flags")" "$rohost"
if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfhost"
    exit
fi

set_ncopy $NCOPY2 $gftmp
wait_for_rep $NCOPY1 $tmpf false \
  "#2 readonly-replica is not removed"

exit_code="$exit_success"
