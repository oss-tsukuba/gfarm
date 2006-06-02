#!/bin/sh

. ./regress.conf

local_script=$localtop/RT_gfrun_script.$$

trap 'gfrm -f $gftmp; rm -f $local_script; exit $exit_trap' $trap_sigs

if echo 'echo OK' >$local_script && chmod +x $local_script
   arch=`gfhost -M \`gfsched -N 1\` | awk '{ print $1 }'` &&
   gfreg -a $arch $local_script $gftmp &&
   [ x"`gfrun $gftmp`" = x"OK" ]
then
	exit_code=$exit_pass
fi

gfrm $gftmp
rm $local_script
exit $exit_code
