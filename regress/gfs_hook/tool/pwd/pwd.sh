#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if cd $hooktop && [ x"`pwd`" = x"$hooktop" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
