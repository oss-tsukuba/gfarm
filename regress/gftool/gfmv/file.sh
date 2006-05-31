#!/bin/sh

. ./regress.conf

trap 'gfrm $gftmp/xxx $gftmp/1byte; gfrmdir $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp &&
   gfreg $data/1byte $gftmp/1byte &&
   gfmv $gftmp/1byte $gftmp/xxx &&
   gfexport $gftmp/xxx | cmp -s - data/1byte; then
	exit_code=$exit_pass
fi

gfrm $gftmp/xxx
gfrmdir $gftmp
exit $exit_code
