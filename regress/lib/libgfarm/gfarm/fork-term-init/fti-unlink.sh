#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if $testbin/fti-unlink $gftmp; then
	exit_code=$exit_pass
fi
exit $exit_code

# NOTE:
# This test case doesn't always detect the problem described in
# [gfarm-developers:01294] and [gfarm-developers:01330].
# You need to use valgrind against fti-unlink.c to see the problem.
