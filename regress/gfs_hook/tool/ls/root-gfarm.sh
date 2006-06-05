#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if dir=`ls -d /gfarm` && [ $dir = "/gfarm" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
