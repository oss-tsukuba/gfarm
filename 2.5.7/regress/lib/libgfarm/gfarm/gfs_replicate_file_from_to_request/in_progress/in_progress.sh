#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

gfsched -w >$localtmp

if [ `sed 2q $localtmp | wc -l` -ne 2 ]; then
    rm -f $localtmp
    exit $exit_unsupported
fi
srchost=`sed -n 1p $localtmp`
dsthost=`sed -n 2p $localtmp`

# XXX there is a race condition that 1st replication finshes before this error
if $testbin/in_progress $gftmp $srchost $dsthost 2>&1 |
	fgrep 'operation already in progress' >/dev/null
then
    exit_code=$exit_pass
fi

gfrm $gftmp
rm -f $localtmp
exit $exit_code
