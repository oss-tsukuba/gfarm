#!/bin/sh

. ./regress.conf

localtmp=$localtop/"`echo $0 | sed s:/:_:g`".$$

trap 'rm -rf $localtmp $hooktmp; exit $exit_trap' $trap_sigs

if mkdir -p $localtmp/data && mkdir -p $hooktmp/data
   cp $data/gftest-0.0.tar.gz $localtmp/data &&
   cp $data/gftest-0.0.tar.gz $hooktmp/data &&
   diff -r $localtmp/data $hooktmp/data >/dev/null; then
	exit_code=$exit_pass
fi

rm -rf $localtmp $hooktmp
exit $exit_code
