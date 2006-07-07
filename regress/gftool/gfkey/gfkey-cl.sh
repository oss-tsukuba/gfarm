#!/bin/sh

. ./regress.conf

key=$HOME/.gfarm_shared_key

# NOTE:
# We won't do "rm -f $key" at exit, because the key may be copied by hand,
# or it may be a symbolic link.

trap 'exit $exit_trap' $trap_sigs

if gfkey -cl && [ -f $key ] &&
   ls -lL $key | awk '{ if ($1 == "-rw-------") exit 0; else exit 1}' &&
   awk '{ if (NF == 2) exit 0; else exit 1}' $key; then
	exit_code=$exit_pass
fi

exit $exit_code
