#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs
trap 'gfrmdir $gftmp; exit $exit_code' 0

if gfmkdir $gftmp && [ x"`gfls -d $gftmp`" = x"$gftmp" ]; then
	exit_code=$exit_pass
fi
