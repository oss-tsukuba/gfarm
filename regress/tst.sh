#!/bin/sh

log=log
fmt="%-50.50s ... %s\n"

for tst
do
	sh $tst </dev/null >>$log 2>&1
	exit_code=$?
	case $exit_code in
	0)	printf "$fmt" "$tst" "PASS";;
	1)	printf "$fmt" "$tst" "FAIL";;
	*)	printf "$fmt" "$tst" "exit code = $exit_code";;
	esac
done
