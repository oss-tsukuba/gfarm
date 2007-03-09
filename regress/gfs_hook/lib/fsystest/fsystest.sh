#!/bin/sh

. ./regress.conf

# "__testfile*" is mistakenly created in current directroy,
# because gfs_hook doesn't work correctly.

trap 'rm -rf $hooktmp; rm -f __testfile*; exit $exit_trap' $trap_sigs

expected=$testbase/index.`uname`.expected
if [ ! -f $expected ]
then
    exit $exit_unsupported
fi
if mkdir $hooktmp &&
    $testbin/fsystest $hooktmp | diff -c - $expected
then
    exit_code=$exit_pass
fi

rm -rf $hooktmp
rm -f __testfile*
exit $exit_code
