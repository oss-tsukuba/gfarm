#!/bin/sh

. ./regress.conf

user=tmpusr-`hostname`-$$
real=tmprealname
home=/home/$user
gsi_dn=""
auth_id="auth_id"

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfuser -d "$user" 2>/dev/null; rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfuser -c "$user" "$real" "$home" "$gsi_dn"; then
    if gfuser -A "$user" "WrongType" "$auth_id" 2>$localtmp; then
	:
    elif grep ': invalid argument$' $localtmp >/dev/null; then
	exit_code=$exit_pass
    fi
    gfuser -d "$user"
fi

rm -f $localtmp
exit $exit_code
