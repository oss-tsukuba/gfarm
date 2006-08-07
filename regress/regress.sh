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
fmt="%-60.60s ... %s"

while read line; do
	set x $line
	shift
	case $# in 0) continue;; esac
	case $1 in '#'*) continue;; esac

	tst=$1
	fin="-------------------------------------------------- --- ----"
	if [ -f $tst ]; then
		( printf "$fmt" "$tst"; echo "BEGIN" ) >>$log
		  printf "$fmt" "$tst"

		# XXX FIXME: Solaris 9 /bin/sh dumps core with sh/test-d.sh
		$shell $tst </dev/null >>$log 2>&1
		exit_code=$?

		case $exit_code in
		$exit_pass)
			( printf "$fmt" "$tst"; echo "PASS" ) >>$log
						echo "PASS"
			pass=`expr $pass + 1`;;
		$exit_fail)
			( printf "$fmt" "$tst"; echo "FAIL" ) >>$log
						echo "FAIL"
			fail=`expr $fail + 1`;;
		*)	case $exit_code in
			$exit_trap)
				( printf "$fmt" "$tst"; echo "KILLED" ) >>$log
							echo "KILLED";;
			*)	( printf "$fmt" "$tst";
				  echo "exit code = $exit_code" ) >>$log
				  echo "exit code = $exit_code";;
			esac
			killed=1
			break;;
		esac
		echo "$fin" >>$log
	else
		( printf "$fmt" "$tst"; echo "SKIPPED" ) >>$log
					echo "SKIPPED"
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
