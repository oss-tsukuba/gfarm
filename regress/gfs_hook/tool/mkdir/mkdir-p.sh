#!/bin/sh

. ./regress.conf

trap 'rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if mkdir -p $hooktmp/$hooktmp && [ -d $hooktmp/$hooktmp ]; then
	exit_code=$exit_pass
fi

rm -rf $hooktmp
exit $exit_code
