#!/bin/sh

. ./regress.conf

# "__testfile" is mistakenly created in current directroy,
# because gfs_hook doesn't work correctly.

trap 'rm -rf $hooktmp; rm -f __testfile; exit $exit_trap' $trap_sigs

if mkdir $hooktmp &&
    $testbin/fsystest $hooktmp | diff -c - $testbase/index.expected
then
	exit_code=$exit_pass
fi

rm -rf $hooktmp
rm -f __testfile
exit $exit_code
