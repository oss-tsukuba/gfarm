#!/bin/sh

. ./regress.conf

host=`gfhost | grep -v '^localhost$' | sed -n '1p'`
localtmp=$localtop/`basename $hooktmp`

trap 'rm -f $hooktmp; ssh $host rm -f $localtmp exit $exit_trap' $trap_sigs

if ! ssh-add -l >/dev/null; then
    exit $exit_unsupported
elif cp $data/1byte $hooktmp &&
    scp $hooktmp $host:$localtmp &&
    ssh $host cat $localtmp | cmp -s $data/1byte -; then
    exit_code=$exit_pass
else
    case `gfarm.arch.guess` in
    *-*-solaris*) # Sun SSH doesn't have this problem.
	:;;
    *)	case $REGRESS_AUTH in
	gsi|gsi_auth)
	    case $exit_code in
	    $exit_pass) exit_code=$exit_xpass;;
	    $exit_fail) exit_code=$exit_xfail;;
	    esac;;
	esac;;
    esac
fi

rm -f $hooktmp
ssh $host rm -f $localtmp
exit $exit_code
