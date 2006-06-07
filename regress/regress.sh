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
fmt="%-50.50s ... %s\n"

while read line; do
	set x $line
	shift
	case $# in 0) continue;; esac
	case $1 in '#'*) continue;; esac

	tst=$1
	fin="-------------------------------------------------- --- ----"
	if [ -f $tst ]; then
		printf "$fmt" "$tst" "BEGIN" >>$log

		sh $tst </dev/null >>$log 2>&1
		exit_code=$?

		case $exit_code in
		$exit_pass)
			printf "$fmt" "$tst" "PASS"
			printf "$fmt" "$tst" "PASS" >>$log
			pass=`expr $pass + 1`;;
		$exit_fail)
			printf "$fmt" "$tst" "FAIL"
			printf "$fmt" "$tst" "FAIL" >>$log
			fail=`expr $fail + 1`;;
		*)	case $exit_code in
			$exit_trap)
				printf "$fmt" "$tst" "KILLED"
				printf "$fmt" "$tst" "KILLED" >>$log;;
			*)	printf "$fmt" "$tst" "exit code = $exit_code"
				printf "$fmt" "$tst" "exit code = $exit_code" \
					>>$log;;
			esac
			killed=1
			break;;
		esac
		echo "$fin" >>$log
	else
		printf "$fmt" "$tst" "SKIPPED"
		printf "$fmt" "$tst" "SKIPPED" >>$log
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
