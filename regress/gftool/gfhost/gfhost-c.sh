#!/bin/sh

. ./regress.conf

host=tmphostname-`hostname`-$$
port=9999
arch=tmparchname
ncpu=16

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfhost -d $host 2>/dev/null; exit $exit_trap' $trap_sigs

if gfhost -c -a $arch -p $port -n $ncpu $host; then
    if gfhost -M $host |
       awk 'BEGIN { status = 1 }
            $1 == "'$arch'" && $2 == '$ncpu' && $3 == "'$host'" &&
	    $4 == '$port' && $5 == 0 { status = 0 } END { exit status }'
    then
	exit_code=$exit_pass
    fi
    gfhost -d $host
fi

exit $exit_code
