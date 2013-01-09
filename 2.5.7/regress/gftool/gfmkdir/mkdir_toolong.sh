#!/bin/sh

. ./regress.conf

# GFS_MAXNAMLEN in <gfarm/gfs.h>
TEST_LEN=256

gftmp=$gftmp`basename "$gftmp" | awk '{
	l='$TEST_LEN'-length($0)
	for (i = 0; i < l; i++) printf "a"
	printf "\n"
	exit
}'`

trap 'gfrmdir $gftmp; rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp 2>$localtmp; then
	:
elif grep ': file name too long$' $localtmp >/dev/null; then
	exit_code=$exit_pass
fi

gfrmdir $gftmp
rm -f $localtmp
exit $exit_code
