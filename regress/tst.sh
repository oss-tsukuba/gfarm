#!/bin/sh

. ./regress.conf
. $regress/account.sh

exec >>$log

for tst
do
	print_header
	$shell $regress/$tst </dev/null 2>&1
	eval_result $?
	exit_code=$?
	if [ $exit_code -eq $exit_trap ]; then
		break
	fi
	print_footer
done
