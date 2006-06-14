#!/bin/sh

. ./regress.conf

trap 'rmdir $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp && rmdir $hooktmp; then
	exit_code=$exit_pass
fi

exit $exit_code
