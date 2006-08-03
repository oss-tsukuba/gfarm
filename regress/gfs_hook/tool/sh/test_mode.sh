#!/bin/sh

. ./regress.conf

trap 'rm -f $hooktmp; exit $exit_trap' $trap_sigs

if cp $data/1byte $hooktmp &&
   chmod $1 $hooktmp && [ $2 $hooktmp ] &&
   chmod 000 $hooktmp && [ ! $2 $hooktmp ]; then
	exit_code=$exit_pass
fi

rm -rf $hooktmp
exit $exit_code
