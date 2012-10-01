#!/bin/sh

. ./regress.conf

sub=$gftmp/sub

trap 'gfrmdir $sub; gfrmdir $gftmp; exit $exit_trap' $trap_sigs

gfnlink()
{
	gfstat $1 | sed -n -e 's/^.*Links: \([0-9]*\).*/\1/p'
}

if gfmkdir $gftmp &&
   [ x"`gfnlink $gftmp`" = x2 ] &&
   gfmkdir $sub &&
   [ x"`gfnlink $gftmp`" = x3 ] &&
   gfrmdir $sub &&
   [ x"`gfnlink $gftmp`" = x2 ] &&
   gfrmdir $gftmp
then
	exit_code=$exit_pass
fi

gfrmdir $sub
gfrmdir $gftmp
exit $exit_code
