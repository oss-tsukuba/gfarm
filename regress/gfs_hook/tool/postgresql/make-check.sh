#!/bin/sh

. ./regress.conf

trap 'cd; rm -rf $hooktmp; exit $exit_trap' $trap_sigs

pg_file_dir="`dirname $hooktop`"
tar_zipped_pg_file="${pg_file_dir}.tar.gz"

if  [ ! -f "$tar_zipped_pg_file" ]; then
    exit $exit_unsupported
fi

if mkdir $hooktmp &&
   cd $hooktmp &&
   gunzip -c <$tar_zipped_pg_file | pax -r &&
   cd `basename $pg_file_dir` &&
   make check
then
    exit_code=$exit_pass	
fi

cd
rm -rf $hooktmp
exit $exit_code
