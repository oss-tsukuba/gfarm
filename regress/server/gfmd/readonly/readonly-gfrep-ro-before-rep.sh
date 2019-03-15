#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

test_gfrep_ro_before_rep

exit_code="$exit_pass"
