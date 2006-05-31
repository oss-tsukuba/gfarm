#!/bin/sh

. ./regress.conf

log=log
rm -f $log

case $# in
0)	schedule=./schedule;;
*)	schedule=$*;;
esac

killed=0
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

		sh $tst >>$log 2>&1
		exit_code=$?

		case $exit_code in
		$exit_pass)
			echo "$msg PASS"
			echo "$msg PASS" >>$log
			pass=`expr $pass + 1`;;
		$exit_fail)
			echo "$msg FAIL"
			echo "$msg FAIL" >>$log
			fail=`expr $fail + 1`;;
		*)	case $exit_code in
			$exit_trap)
				echo "$msg KILLED"
				echo "$msg KILLED" >>$log;;
			*)	echo "$msg exit code = $exit_code"
				echo "$msg exit code = $exit_code" >>$log;;
			esac
			killed=1
			break;;
		esac
		echo "$fin" >>$log
	else
		echo "$msg SKIPPED" >>$log
		skip=`expr $skip + 1`
	fi
	
done < $schedule

echo ""
echo "Total test: `expr $pass + $fail`"
echo "  success : $pass"
echo "  failure : $fail"

if [ $skip != 0 ]; then
echo ""
echo "  skipped : $skip"
fi

case $killed in 1) exit $exit_trap;; esac
[ $fail = 0 ]
