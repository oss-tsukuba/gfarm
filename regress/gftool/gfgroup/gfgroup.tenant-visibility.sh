#!/bin/sh

. ./regress.conf

if $regress/bin/am_I_gfarm_super_adm; then
    exit $exit_unsupported
fi

# XXX: this assumes there is at least one group in non-default teanat,
# but there is no way to check it by this user.

if gfgroup | fgrep '+'; then
    :
else
    exit_code=$exit_pass
fi
exit $exit_code
