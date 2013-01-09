#!/bin/sh

. ./regress.conf

user=tmpusr-`hostname`-$$
real=tmprealname
home=/home/$user
gsi_dn=''

real2=tmprealname2
home2=/home/$user/
gsi_dn2="/C=JP/O=University of Tsukuba/OU=HPCS/OU=hpcc.jp/CN=John Smith `hostname` $$"

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfuser -d "$user" 2>/dev/null; exit $exit_trap' $trap_sigs

if gfuser -c "$user" "$real" "$home" "$gsi_dn"; then
    if gfuser -m "$user" "$real2" "$home2" "$gsi_dn2"; then
	if gfuser -l "$user" |
	   awk -F: 'BEGIN { status = 1 }
		$1 == "'"$user"'" && $2 == "'"$real2"'" && $3 == "'"$home2"'" &&
		$4 == "'"$gsi_dn2"'" { status = 0 } END { exit status }'
	then
	    exit_code=$exit_pass
	fi
    fi
    gfuser -d "$user"
fi

exit $exit_code
