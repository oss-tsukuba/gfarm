#!/bin/sh

. ./regress.conf

case $# in
1)	gfarm_url=$1;;
*)	echo "Usage: $0 <gfarm-URL>" >&2
	exit $exit_fail;;
esac

if gfrm -r $gfarm_url && [ x"`gfls $gftmp`" = x"" ]; then
    exit_code=$exit_pass
fi

exit $exit_code
