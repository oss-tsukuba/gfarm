#!/bin/sh

. ./regress.conf

trap 'rmdir $hooktmp/yyy $hooktmp/xxx $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp &&
   mkdir $hooktmp/xxx &&
   mv $hooktmp/xxx $hooktmp/yyy &&
   [ x"`ls -d $hooktmp/xxx`" = x"" ] &&
   [ x"`ls -d $hooktmp/yyy`" = x"$hooktmp/yyy" ] &&
   rmdir $hooktmp/yyy
then
	exit_code=$exit_pass
fi

rmdir $hooktmp
exit $exit_code
