#!/bin/sh

. ./regress.conf

trap 'rm -f $hooktmp; exit $exit_trap' $trap_sigs

if cp $data/0byte $hooktmp &&
   chmod 644 $hooktmp &&
   [ x"`ls -l $hooktmp | awk '{ print $1 }'`" = x"-rw-r--r--" ]; then 
	exit_code=$exit_pass
fi

rm -f $hooktmp
exit $exit_code
