#!/bin/sh

. ./regress.conf

user=tmpusr-`hostname`-$$
real=tmprealname
home=/home/$user
gsi_dn="/C=JP/O=University of Tsukuba/OU=HPCS/OU=hpcc.jp/CN=John Smith `hostname` $$"

# more than 1024 characters (GFARM_USER_GSI_DN_MAX in gfm_proto.h)
gsi_dn2="/C=JP/O=University of Tsukuba/OU=HPCS/OU=hpcc.jp/CN=John Smith `hostname` $$ hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh"

if $regress/bin/am_I_gfarmadm; then
    :
else
    exit $exit_unsupported
fi

trap 'gfuser -d "$user" 2>/dev/null; rm -f $localtmp; exit $exit_trap' $trap_sigs

if gfuser -c "$user" "$real" "$home" "$gsi_dn"; then
    if gfuser -m "$user" "$real" "$home" "$gsi_dn2" 2>$localtmp; then
	:
    elif grep ': invalid argument$' $localtmp >/dev/null; then
	exit_code=$exit_pass
    fi
    gfuser -d "$user"
fi

rm -f $localtmp
exit $exit_code
