#!/bin/sh

log=log
fmt="%-50.50s ... "

for tst
do
	printf "$fmt" "$tst"
	sh $tst </dev/null >>$log 2>&1
	exit_code=$?
	case $exit_code in
	0)	echo "PASS";;
	1)	echo "FAIL";;
	*)	echo "exit code = $exit_code";;
	esac
done
