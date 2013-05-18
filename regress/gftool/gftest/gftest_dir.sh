#!/bin/sh

. ./regress.conf
gftmpdir=$gftmp.d

trap 'gfrmdir $gftmpdir; exit $exit_trap' $trap_sigs

if gfmkdir $gftmpdir && gftest -d $gftmpdir; then
    exit_code=$exit_pass
fi

gfrmdir $gftmpdir
exit $exit_code
