#!/bin/sh

. ./regress.conf

localtmp=$localtop/`basename $hooktmp`

trap 'rm -f $localtmp $hooktmp; exit $exit_trap' $trap_sigs

if echo foo >$localtmp && cp $localtmp $hooktmp && grep foo $hooktmp; then
	exit_code=$exit_pass
fi

rm -rf $localtmp $hooktmp
exit $exit_code
