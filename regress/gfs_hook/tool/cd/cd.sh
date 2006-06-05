#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if sh -c 'cd /gfarm'; then
	exit_code=$exit_pass
fi

exit $exit_code
