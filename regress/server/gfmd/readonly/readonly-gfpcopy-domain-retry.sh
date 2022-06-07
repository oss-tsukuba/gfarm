#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

test_gfpcopy_write_to_readonly_domain_and_retry

exit_code="$exit_pass"
