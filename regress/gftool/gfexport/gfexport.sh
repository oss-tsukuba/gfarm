#!/bin/sh

. ./regress.conf

gfexport_out=$localtop/RT_gfexport_out.$$

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $gfexport_out; gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfreg $datafile $gftmp &&
   gfexport $gftmp >$gfexport_out &&
   cmp -s $gfexport_out $datafile; then
	exit_code=$exit_pass
fi

rm -f $gfexport_out
gfrm $gftmp
exit $exit_code
