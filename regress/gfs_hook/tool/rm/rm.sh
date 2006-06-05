#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $hooktmp; exit $exit_trap' $trap_sigs

if cp $datafile $hooktmp &&
   rm $hooktmp && [ x"`ls $hooktmp`" = x"" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
