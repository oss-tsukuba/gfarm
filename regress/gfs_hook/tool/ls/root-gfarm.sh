#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if dir=`ls -d $hooktop` && [ x"$dir" = x"$hooktop" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
