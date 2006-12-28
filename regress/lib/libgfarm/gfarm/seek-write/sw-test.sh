#!/bin/sh

. ./regress.conf

if $testbin/sw-test $gftmp; then
	exit_code=$exit_pass
else
	# until Bugzilla-ja#78 is fixed.
	exit_code=$exit_xfail
fi
exit $exit_code
