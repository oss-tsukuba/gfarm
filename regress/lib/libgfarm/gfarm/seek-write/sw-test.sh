#!/bin/sh

. ./regress.conf

if $testbin/sw-test $gftmp; then
	exit_code=$exit_pass
fi
exit $exit_code
