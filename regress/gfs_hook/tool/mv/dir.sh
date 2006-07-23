#!/bin/sh

. ./regress.conf

trap 'rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp &&
   mkdir $hooktmp/xxx &&
   mkdir $hooktmp/xxx/sub &&
   cp $data/0byte $hooktmp/xxx &&
   cp $data/1byte $hooktmp/xxx/sub &&
   mv $hooktmp/xxx $hooktmp/yyy &&
   [ x"`ls -d $hooktmp/xxx`" = x"" ] &&
   [ x"`ls -d $hooktmp/yyy`" = x"$hooktmp/yyy" ] &&
   [ x"`ls -d $hooktmp/yyy/sub`" = x"$hooktmp/yyy/sub" ] &&
   cmp $data/0byte $hooktmp/yyy/0byte &&
   cmp $data/1byte $hooktmp/yyy/sub/1byte &&
   rm -rf $hooktmp/yyy
then
	exit_code=$exit_pass
fi

rm -rf $hooktmp
exit $exit_code
