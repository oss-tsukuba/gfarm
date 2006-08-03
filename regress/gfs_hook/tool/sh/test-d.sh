#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if [ -d $hooktop ] && rm -rf $hooktop/* && [ ! -d $hooktop/$hooktop ]; then
	exit_code=$exit_pass
fi

exit $exit_code
