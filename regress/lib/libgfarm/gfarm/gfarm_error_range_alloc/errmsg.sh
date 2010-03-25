#!/bin/sh

. ./regress.conf

trap 'exit $exit_trap' $trap_sigs

if [ x"`$testbin/errmsg 1879048191`" = x"unknown error" ] &&
   [ x"`$testbin/errmsg 1879048192`" = x"hoge" ] &&
   [ x"`$testbin/errmsg 1879048193`" = x"piyo" ] &&
   [ x"`$testbin/errmsg 1879048194`" = x"chome" ] &&
   [ x"`$testbin/errmsg 1879048195`" = x"unknown error" ]; then
	exit_code=$exit_pass
fi

exit $exit_code
