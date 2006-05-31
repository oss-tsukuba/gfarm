#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'gfrm $gftmp; exit $exit_trap' $trap_sigs

datasize=`ls -l $datafile | awk '{print $5}'`

if gfreg $datafile $gftmp && [ x"`gfls $gftmp`" = x"$gftmp" ] &&
   [ x"`gfls -l $gftmp | awk '{print $4}'`" = x"$datasize" ]; then
	exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
