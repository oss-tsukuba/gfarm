#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if gfps >/dev/null; then 
	exit_code=$exit_pass
fi

exit $exit_code
