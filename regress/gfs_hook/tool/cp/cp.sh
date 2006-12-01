#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $hooktmp; exit $exit_trap' $trap_sigs

datasize=`ls -l $datafile | awk '{print $5}'`

if cp $datafile $hooktmp && [ x"`ls $hooktmp`" = x"$hooktmp" ] &&
   [ x"`ls -l $hooktmp | awk '{print $5}'`" = x"$datasize" ]; then
	exit_code=$exit_pass
fi

rm -f $hooktmp
exit $exit_code
