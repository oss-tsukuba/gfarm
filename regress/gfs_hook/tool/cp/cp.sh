#!/bin/sh

. ./regress.conf

destfile=/gfarm/$USER/RT_cp_file.$$

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $destfile exit $exit_trap' $trap_sigs

datasize=`ls -l $datafile | awk '{print $5}'`

if cp $datafile $destfile && [ x"`ls $destfile`" = x"$destfile" ] &&
   [ x"`ls -l $destfile | awk '{print $5}'`" = x"$datasize" ]; then
	exit_code=$exit_pass
fi

rm -f $destfile
exit $exit_code
