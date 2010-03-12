#!/bin/sh

. ./regress.conf

trap '$exit_trap' $trap_sigs

if $testbin/all_errno | awk '$1 != 0 && $2 == 0 { exit 1 }'
then
    exit_code=$exit_pass
fi

exit $exit_code
