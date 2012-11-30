#!/bin/sh

# https://sourceforge.net/apps/trac/gfarm/ticket/456

. ./regress.conf

[ `gfsched -w | wc -l` -ge 2 ] || exit $exit_unsupported

WAIT_TIME_LIMIT=10

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

if gfncopy -s 2 $gftmp; then
	:
else
	echo failed gfxattr
	clean
	exit $exit_fail
fi

# -W1 is needed to make gfsd processes access to gfmd simultaneously
echo 123456789 | $gfs_pio_test -c -w -W1 -P1 -W9 $tmpf &
echo abcdefghi | $gfs_pio_test -c -w -W1 -P1 -W9 $tmpf &
echo jklmnopqr | $gfs_pio_test -c -w -W1 -P1 -W9 $tmpf &
echo stuvwxyz_ | $gfs_pio_test -c -w -W1 -P1 -W9 $tmpf &
wait

WAIT_TIME=0
while
	if gfstat $tmpf > $statf 2>&1; then
		:
	else
		echo failed gfstat
		cat $statf
		clean
		exit $exit_fail
	fi
	if [ `awk '/Ncopy/{print $NF}' $statf` -eq 2 ]; then
		exit_code=$exit_pass
		false # exit from this loop
	else
		true
	fi
do
	WAIT_TIME=`expr $WAIT_TIME + 1`
	if [ $WAIT_TIME -gt $WAIT_TIME_LIMIT ]; then
		echo replication timeout
		clean
		exit $exit_fail
	fi
	sleep 1
done

clean
exit $exit_code
