#!/bin/sh

. ./regress.conf

host=`gfhost | grep -v '^localhost$' | sed -n '1p'`
localtmp=$localtop/`basename $hooktmp`

trap 'rm -f $hooktmp; ssh $host rm -f $localtmp; exit $exit_trap' $trap_sigs

if ! ssh-add -l >/dev/null; then
    exit $exit_unsupported
elif scp $data/1byte $host:$localtmp &&
    scp $host:$localtmp $hooktmp &&
    cmp -s $data/1byte $hooktmp
then
    exit_code=$exit_pass
fi

rm -f $hooktmp
ssh $host rm -f $localtmp
exit $exit_code
