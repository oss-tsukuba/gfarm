#!/bin/sh

. ./regress.conf

tmpfile=/gfarm/$USER/RT_rm_file.$$

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $tmpfile; exit $exit_trap' $trap_sigs

if cp $datafile $tmpfile &&
   rm $tmpfile && [ x"`ls $tmpfile`" = x"" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
