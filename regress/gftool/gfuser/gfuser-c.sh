#!/bin/sh

. ./regress.conf

user=tmpusr-`hostname`-$$
real=tmprealname
home=/home/$user
gsi_dn=''

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfuser -d "$user" 2>/dev/null; exit $exit_trap' $trap_sigs

if gfuser -c "$user" "$real" "$home" "$gsi_dn"; then
    if gfuser -l "$user" |
       awk -F: 'BEGIN { status = 1 }
            $1 == "'"$user"'" && $2 == "'"$real"'" && $3 == "'"$home"'" &&
	    $4 == "'"$gsi_dn"'" { status = 0 } END { exit status }'
    then
	exit_code=$exit_pass
    fi
    gfuser -d "$user"
fi

exit $exit_code
