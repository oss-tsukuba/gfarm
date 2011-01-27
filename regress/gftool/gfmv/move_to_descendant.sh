#!/bin/sh

. ./regress.conf

sub=$gftmp/sub

trap 'rm -f $localtmp; gfrmdir $sub $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp $sub; then
	if gfmv $gftmp $sub 2>$localtmp; then
		:
	elif grep 'invalid argument' $localtmp >/dev/null; then
		exit_code=$exit_pass
	fi
fi

rm -f $localtmp
gfrmdir $sub $gftmp
exit $exit_code
