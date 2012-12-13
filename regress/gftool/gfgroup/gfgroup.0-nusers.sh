#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarmadm; then
    [ `gfuser | wc -l` -ge 2 ] || exit $exit_unsupported
else
    exit $exit_unsupported
fi

tmpgroup=tmpgrp-`hostname`-$$
user1=`gfuser | sed -n 1p`

trap 'gfgroup -d "$tmpgroup" 2>/dev/null; exit $exit_trap' $trap_sigs

#
# Test "gfgroup -m GROUP" with no user name.
#
gfgroup_m_result=false
if gfgroup -c "$tmpgroup" "$user1"; then
    if gfgroup -m "$tmpgroup"; then
	if [ X"`gfgroup -l $tmpgroup`" = X"$tmpgroup:" ]; then
	    gfgroup_m_result=true
	fi
    fi
    gfgroup -d "$tmpgroup" 2>/dev/null
fi

#
# Test "gfgroup -c GROUP" with no user name.
#
gfgroup_c_result=false
if gfgroup -c "$tmpgroup"; then
    if [ X"`gfgroup -l $tmpgroup`" = X"$tmpgroup:" ]; then
	gfgroup_c_result=true
    fi
    gfgroup -d "$tmpgroup" 2>/dev/null
fi

if [ $gfgroup_m_result = true -a $gfgroup_c_result = true ]; then
    exit_code=$exit_pass
fi

exit $exit_code
