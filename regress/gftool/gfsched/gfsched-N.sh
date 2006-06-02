#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if host=`gfsched -N 1` && 
   gfhost -M $host | awk '{ if ($3 == "'$host'") exit 0; else exit 1 }'; then
	exit_code=$exit_pass
fi

exit $exit_code
