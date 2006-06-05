#!/bin/sh

. ./regress.conf

trap 'gfrm -f gfarm:"`basename $hooktmp`"; exit $exit_trap' $trap_sigs

if cp $data/0byte $hooktmp &&
   chmod 755 $hooktmp &&
   [ x"`ls -l $hooktmp | awk '{ print $1 }'`" = x"-rwxr-xr-x" ]; then 
	exit_code=$exit_pass
fi

gfrm -f gfarm:"`basename $hooktmp`"
exit $exit_code
