#!/bin/sh

. ./regress.conf

trap 'rm -f $hooktmp; exit $exit_trap' $trap_sigs

if touch $hooktmp && [ -f $hooktmp ] && [ ! -s $hooktmp ]; then
	exit_code=$exit_pass
fi

rm -f $hooktmp
exit $exit_code
