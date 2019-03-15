#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

rohost="$(gfsched -w | head -n 1)"
flags="$(query_host_flags "$rohost")"

test_set_readonly_flag "$rohost" "$flags" true
test_unset_readonly_flag "$rohost" "$flags" true

exit_code="$exit_pass"
