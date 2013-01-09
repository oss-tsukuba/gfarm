#!/bin/sh

log=log

for tst
do
	msg="`echo $tst | awk '{printf "%-50.50s ... ", $1}'`"
	sh $tst >>$log 2>&1
	exit_code=$?
	case $exit_code in
	0)	echo "$msg PASS";;
	1)	echo "$msg FAIL";;
	*)	echo "$msg exit code = $exit_code";;
	esac
done
