#!/bin/sh

. ./regress.conf

gfimport_text_out=$localtop/RT_gfimport_text_out.$$

case $# in
1)	datafile=$1;;
*)	echo "Usage: $0 <datafile>" >&2
	exit $exit_fail;;
esac

trap 'rm -f $gfimport_text_out; gfrm $gftmp; exit $exit_trap' $trap_sigs

if gfhost | head -2 |  gfimport_text -H - -o $gftmp $datafile &&
   gfexport $gftmp >$gfimport_text_out &&
   cmp -s $gfimport_text_out $datafile; then
	exit_code=$exit_pass
fi

rm -f $gfimport_text_out
gfrm $gftmp
exit $exit_code
