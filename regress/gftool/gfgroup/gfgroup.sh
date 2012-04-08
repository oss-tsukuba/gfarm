#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarmadm; then
    [ `gfuser | wc -l` -ge 2 ] || exit $exit_unsupported
else
    exit $exit_unsupported
fi

tmpgroup=tmpgrp-`hostname`-$$
user1=`gfuser | sed -n 1p`
user2=`gfuser | sed -n 2p`

trap 'gfgroup -d "$tmpgroup" 2>/dev/null; exit $exit_trap' $trap_sigs

if gfgroup -c "$tmpgroup" "$user1"; then
    if [ X"`gfgroup -l $tmpgroup`" = X"$tmpgroup: $user1" ]; then
	if gfgroup -m "$tmpgroup" "$user1" "$user2"; then
	    if [ X"`gfgroup -l $tmpgroup`" = X"$tmpgroup: $user1 $user2" -o \
		 X"`gfgroup -l $tmpgroup`" = X"$tmpgroup: $user2 $user1" ]; then
		if gfgroup -d "$tmpgroup"; then
		    if gfgroup -l "$tmpgroup" 2>/dev/null; then
			:
		    else
			exit_code=$exit_pass
		    fi
		fi
	    fi
	fi
    fi
    gfgroup -d "$tmpgroup" 2>/dev/null
fi

exit $exit_code
