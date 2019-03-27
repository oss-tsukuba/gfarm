#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

gfmd_restart_all=$regress/bin/gfmd_restart_all
if ! [ -x "$gfmd_restart_all" ]; then
  exit_code="$exit_unsupported"
  exit
fi

rohost="$(gfsched -w | head -n 1)"
flags="$(query_host_flags "$rohost")"

test_set_readonly_flag "$rohost" "$flags"  "$gfmd_restart_all"
test_unset_readonly_flag "$rohost" "$flags" "$gfmd_restart_all"

exit_code="$exit_pass"
