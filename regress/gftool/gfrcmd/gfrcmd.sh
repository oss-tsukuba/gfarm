#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

host=`gfhost | head -1`

if [ x"`gfrcmd $host /bin/echo OK`" = x"OK" ]; then 
	exit_code=$exit_pass
fi

exit $exit_code
