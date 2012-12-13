#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarmadm; then
    [ `gfuser | wc -l` -ge 2 ] || exit $exit_unsupported
else
    exit $exit_unsupported
fi

tmpgroup=tmpgrp-`hostname`-$$

trap 'gfgroup -d "$tmpgroup" 2>/dev/null; exit $exit_trap' $trap_sigs

if gfgroup -c "$tmpgroup"; then
    if [ X"`gfgroup -l $tmpgroup`" = X"$tmpgroup:" ]; then
	exit_code=$exit_pass
    fi
    gfgroup -d "$tmpgroup" 2>/dev/null
fi

exit $exit_code
