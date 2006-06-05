#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if gfpwd | awk '{ if ($1 ~ /^gfarm:\//) exit 0; else exit 1}'; then
	exit_code=$exit_pass
fi

exit $exit_code
