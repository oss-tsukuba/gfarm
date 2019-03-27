#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

exit_code="$exit_xfail"
test_gfrep_ro_before_rep

exit_code="$exit_xpass"
