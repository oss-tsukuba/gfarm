#!/bin/sh

. ./regress.conf

trap '$exit_trap' $trap_sigs

if $testbin/conn_cache_test; then
	exit_code=$exit_pass
fi

exit $exit_code
