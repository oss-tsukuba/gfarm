#!/bin/sh

. ./regress.conf

trap 'rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp &&
   dir=`pwd` && cd $hooktmp &&
   gzip -cd $dir/data/gftest-0.0.tar.gz | pax -r &&
   cd gftest-0.0 &&
   env CONFIG_SHELL=`which $shell` $shell ./configure &&
   make; then
	exit_code=$exit_pass
fi

cd $dir
rm -rf $hooktmp
exit $exit_code
