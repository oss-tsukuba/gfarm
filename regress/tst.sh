#!/bin/sh

. ./account.sh

log=log
exec >>$log

clear_counters
fmt_init

for tst
do
	print_header
	$shell $tst </dev/null 2>&1
	eval_result $?
	exit_code=$?
	if [ $exit_code -eq $exit_trap ]; then
		break
	fi
done
