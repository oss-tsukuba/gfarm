#!/bin/sh

. ./regress.conf

localtmp=$localtop/"`echo $0 | sed s:/:_:g`".$$

trap 'rm -rf $localtmp $hooktmp; exit $exit_trap' \
     $trap_sigs

if mkdir $localtmp && mkdir $hooktmp &&
   dir=`pwd` && cd $localtmp &&
   gzip -cd $dir/data/gftest-0.0.tar.gz | pax -r &&
   cd $hooktmp &&
   gzip -cd $dir/data/gftest-0.0.tar.gz | pax -r &&
   diff -r $localtmp/gftest-0.0 $hooktmp/gftest-0.0 >/dev/null; then
	exit_code=$exit_pass
fi

rm -rf $localtmp $hooktmp
exit $exit_code
