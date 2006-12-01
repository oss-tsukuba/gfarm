#!/bin/sh

. ./regress.conf

if [ `GFARM_TEST_PRINT_ARGV=true $testbin/init | wc -l` -eq 1 ]; then
	exit_code=$exit_pass
fi

exit $exit_code
