#!/bin/sh

log=log
rm -f $log

pass=0
fail=0
skip=0

while read line; do
	set x $line
	shift
	case $# in 0) continue;; esac
	case $1 in '#'*) continue;; esac

	tst=$1
	msg="`echo $tst | awk '{printf "%-50.50s ... ", $1}'`"
	fin="-------------------------------------------------- --- ----"
	if [ -x $tst ]; then
		echo "$msg BEGIN" >>$log
		if sh $tst >>$log 2>&1; then
			echo "$msg PASS"
			echo "$msg PASS" >>$log
			pass=`expr $pass + 1`
		else
			echo "$msg FAIL"
			echo "$msg FAIL" >>$log
			fail=`expr $fail + 1`
		fi
		echo "$fin" >>$log
	else
		echo "$msg SKIPPED" >>$log
		skip=`expr $skip + 1`
	fi
	
done < schedule

echo ""
echo "Total test: `expr $pass + $fail`"
echo "  success : $pass"
echo "  failure : $fail"

if [ $skip != 0 ]; then
echo ""
echo "  skipepd : $skip"
fi

[ $fail = 0 ]
