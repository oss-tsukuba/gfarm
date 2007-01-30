#!/bin/sh

. ./regress.conf

# NOTE: this redirects stdout to $log
. $regress/account.sh

fmt_init

for tst
do
	print_header
	$shell $regress/$tst </dev/null 2>&1
	eval_result $?
	exit_code=$?
	if [ $exit_code -eq $exit_trap ]; then
		break
	fi
done
