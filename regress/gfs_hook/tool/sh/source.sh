#!/bin/sh

. ./regress.conf

localtmp=$localtop/`basename $hooktmp`

trap 'rm -f $localtmp $hooktmp; exit $exit_trap' $trap_sigs

if echo foo=bar >$localtmp &&
   cp $localtmp $hooktmp &&
   . $hooktmp &&
   [ x$foo = x"bar" ]
then
	exit_code=$exit_pass
fi

rm -rf $localtmp $hooktmp
exit $exit_code
