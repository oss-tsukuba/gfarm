#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

test_gfrep

exit_code="$exit_pass"
