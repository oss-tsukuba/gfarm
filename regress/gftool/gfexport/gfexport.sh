#!/bin/sh

. ./regress.conf

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $localtmp; gfrm $gftmp; exit $exit_trap' $trap_sigs

datasize=`ls -l $datafile | awk '{print $5}'`

if gfreg $datafile $gftmp &&
   gfexport $gftmp >$localtmp &&
   cmp -s $localtmp $datafile; then
	exit_code=$exit_pass
fi

rm -f $localtmp
gfrm $gftmp
exit $exit_code
