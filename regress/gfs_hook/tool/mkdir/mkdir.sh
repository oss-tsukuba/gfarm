#!/bin/sh

. ./regress.conf

trap 'rmdir $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp && [ -d $hooktmp ]; then
	exit_code=$exit_pass
fi

rmdir $hooktmp
exit $exit_code
