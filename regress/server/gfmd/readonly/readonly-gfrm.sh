#!/bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

hosts="$(gfsched -w)"  # use writable hosts only
rohost="$(echo "$hosts" | head -n 1)"
flags="$(query_host_flags "$rohost")"
if [ "$?" -ne 0 ] || [ "X${flags}" = X ]; then
    echo "failed: $0"
    exit
fi

test_remove_readonly_replica() {
  host="$1"
  flags="$2"
  diag=test_remove_readonly_replica

  gfreg -h "$host" "${data}/1byte" "gfarm:${gftmp}/test1"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfreg -h ${host}"
    exit
  fi

  gfhost -m -f "$(set_readonly_flag "$flags")" "$host"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfhost"
    exit
  fi

  gfrm -h "$host" "${gftmp}/test1"
  if [ "$?" -eq 0 ]; then
    echo "unexpected success: ${diag}: gfrm -h ${host}"
    exit
  fi

  gfrm "${gftmp}/test1"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfrm"
    exit
  fi
}

test_remove_readonly_replica "$rohost" "$flags"

exit_code="$exit_pass"
