#!/bin/sh

. ./regress.conf

host=`gfhost | sed -n '1p'`
localtmp=$localtop/`basename $hooktmp`

trap 'rm -f $hooktmp; ssh $host rm -f $localtmp exit $exit_trap' $trap_sigs

if ! ssh-add -l >/dev/null; then
    exit $exit_untested
elif cp $data/1byte $hooktmp &&
    scp $hooktmp $host:$localtmp &&
    ssh $host cat $localtmp | cmp -s $data/1byte -; then
    exit_code=$exit_pass
fi

rm -f $hooktmp
ssh $host rm -f $localtmp
exit $exit_code
