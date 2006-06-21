#!/bin/sh

. ./regress.conf

# NOTE:
# `gfrun' internally invokes `gfexec' on the remote host without specifying
# absolute pathname, thus, 

trap 'gfrm -f $gftmp; exit $exit_trap' $trap_sigs

if arch=`gfhost -M \`gfsched -N 1\` | awk '{ print $1 }'` &&
   gfreg -a $arch $data/ok.sh $gftmp &&
   [ x"`gfrun $gftmp`" = x"OK" ]
then
	exit_code=$exit_pass
fi

gfrm $gftmp
exit $exit_code
