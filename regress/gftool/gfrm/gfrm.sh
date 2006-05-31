#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfreg $datafile $gftmp &&
   gfrm $gftmp && [ x"`gfls $gftmp`" = x"" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
