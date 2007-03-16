#!/bin/sh

. ./regress.conf

loop=1
fail=0

while :; do
	echo "############## loop = $loop (fail = $fail) #################"

	$regress/check.sh ${1+"$@"}
	exit_code=$?

	case $exit_code in
	$exit_pass)
		:;;
	$exit_fail)
		echo "FAILURE @ loop $loop"
		mv log log.$loop
		fail=`expr $fail + 1`;;
	$exit_trap)
		exit $exit_code;;
	*)
		echo "`basename $0`: unknown exit code $exit_code" >&2
		exit $exit_code;;
	esac
	loop=`expr $loop + 1`
done
