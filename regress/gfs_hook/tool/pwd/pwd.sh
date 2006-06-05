#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if [ x"`sh -c 'cd /gfarm && pwd'`" = x"/gfarm" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
