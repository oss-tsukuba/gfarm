#!/bin/sh

. ./regress.conf

key=$HOME/.gfarm_shared_key

trap 'rm -f $key; exit $exit_trap' $trap_sigs

if gfkey -cl && [ -f $key ] &&
   [ x"`ls -l $key | awk '{ print $1 }'`" = x"-rw-------" ] &&
   awk '{ if (NF == 2) exit 0; else exit 1}' $key; then
	exit_code=$exit_pass
fi

rm -f $key
exit $exit_code
