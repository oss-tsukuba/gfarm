#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if [ x"`gfrun -u echo OK`" = x"OK" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
