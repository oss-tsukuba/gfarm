#!/bin/sh

. ./regress.conf

[ `gfsched -w | wc -l` -ge 2 ] || exit $exit_unsupported

WAIT_TIME=2

gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
tmpf=$gftmp/foo
statf=$localtmp

clean() {
	gfrm -f $tmpf
	gfrmdir $gftmp
	rm -f $statf
}

trap 'clean; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp; then
	:
else
	echo failed gfmkdir
	exit $exit_fail
fi

if echo -n 2 | gfxattr -s $gftmp gfarm.ncopy; then
	:
else
	echo failed gfxattr
	exit $exit_fail
fi

if echo 123456789 | $gfs_pio_test -c -w -W10 $tmpf; then
	:
else
	echo failed $gfs_pio_test
	exit $exit_fail
fi

sleep $WAIT_TIME
if gfstat $tmpf > $statf 2>&1; then
	:
else
	echo failed gfstat
	cat $statf
	exit $exit_fail
fi

if [ `awk '/Ncopy/{print $NF}' $statf` -eq 2 ]; then
	exit_code=$exit_pass
else
	echo failed gfstat
	exit $exit_fail
fi

clean
exit $exit_code
