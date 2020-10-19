#!/bin/sh

test_for_gfarmroot=false
base="$(dirname "$0")"
. "${base}/effective_perm.common.sh"

if $regress/bin/am_I_gfarmroot; then
    exit $exit_unsupported
fi

## owner
test_ep "000" "---"
test_ep "700" "rwx"
test_ep "400" "r--"
test_ep "200" "-w-"
test_ep "100" "--x"
exit_code=$exit_pass

cleanup
exit $exit_code
