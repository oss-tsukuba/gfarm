#!/bin/sh

. ./regress.conf

usage() {
    echo "Usage: $0 <mode> <'possible' or 'impossible'>" >&2
    exit $exit_fail
}

case $# in
2)
    case $2 in
    "possible"|"impossible")
	mode=$1; possibility=$2;;
    *) usage;;
    esac;;	
*)
    usage;; 
esac

trap 'rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp &&
   chmod 300 $hooktmp &&
   touch $hooktmp/foo && chmod 000 $hooktmp/foo &&
   chmod $mode $hooktmp 
then
   if ([ x$possibility = x"possible" ] && mv $hooktmp/foo $hooktmp/bar 2>/dev/null) ||
      ([ x$possibility = x"impossible" ] && ! mv $hooktmp/foo $hooktmp/bar 2>/dev/null)
   then
       exit_code=$exit_pass
   else 
       exit_code=$exit_xfail
   fi    
fi

chmod 700 $hooktmp
rm -rf $hooktmp
exit $exit_code
