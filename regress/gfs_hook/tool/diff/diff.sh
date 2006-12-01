#!/bin/sh

. ./regress.conf

trap 'rm -f $hooktmp; exit $exit_trap' \
     $trap_sigs

if cp $data/gftest-0.0.tar.gz $hooktmp &&
   diff $data/gftest-0.0.tar.gz $hooktmp >/dev/null; then
	exit_code=$exit_pass
fi

rm -f $hooktmp
exit $exit_code
