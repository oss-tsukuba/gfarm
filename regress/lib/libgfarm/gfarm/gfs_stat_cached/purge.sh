#!/bin/sh

. ./regress.conf

clean() {
	rm -f $localtmp > /dev/null 2>&1
	gfrm -f $gftmp > /dev/null 2>&1
}

trap 'clean; exit $exit_trap' $trap_sigs

echo a > $localtmp
if $testbin/gfs_stat_cached_test -P $localtmp $gftmp; then :
else
	exit $exit_fail
fi

clean
exit $exit_pass
