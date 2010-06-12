#!/bin/sh

. ./regress.conf

# constants
account_bgfmt="--- %-60.60s %s\n"
account_lgfmt="@:= %-60.60s %s\n"
account_fmt="%-60.60s ... "
account_fin="--- ------------------------------------------------------------ ----"

log=log
rm -f $log

case $# in
0)	schedule=$regress/schedule;;
*)	schedule=$*;;
esac

n_pass=0
n_fail=0
n_xpass=0
n_xfail=0
n_unresolved=0
n_untested=0
n_unsupported=0
n_trap=0

while read line; do
	set x $line
	shift
	case $# in 0) continue;; esac
	case $1 in '#'*) continue;; esac

	tst=$1

	printf -- "$account_fmt"   "$tst"
	printf -- "$account_bgfmt" "$tst" "BEGIN" >>$log
	date +'@@_ start at %s - %Y-%m-%d %H:%M:%S' >>$log

	if [ -x $regress/$tst ]; then

		sh $regress/$tst >>$log 2>&1
		exit_code=$?

		case $exit_code in
		$exit_pass)
			echo                              "PASS"
			printf -- "$account_lgfmt" "$tst" "PASS" >>$log
			n_pass=`expr $n_pass + 1`;;
		$exit_fail)
			echo                              "FAIL"
			printf -- "$account_lgfmt" "$tst" "FAIL" >>$log
			n_fail=`expr $n_fail + 1`;;
		$exit_xpass)
			echo                              "XPASS"
			printf -- "$account_lgfmt" "$tst" "XPASS" >>$log
			n_xpass=`expr $n_xpass + 1`;;
		$exit_xfail)
			echo                              "XFAIL"
			printf -- "$account_lgfmt" "$tst" "XFAIL" >>$log
			n_xfail=`expr $n_xfail + 1`;;
		$exit_unresolved)
			echo                              "UNRESOLVED"
			printf -- "$account_lgfmt" "$tst" "UNRESOLVED" >>$log
			n_unresolved=`expr $n_unresolved + 1`;;
		$exit_untested)
			echo                              "UNTESTED"
			printf -- "$account_lgfmt" "$tst" "UNTESTED" >>$log
			n_untested=`expr $n_untested + 1`;;
		$exit_unsupported)
			echo                              "UNSUPPORTED"
			printf -- "$account_lgfmt" "$tst" "UNSUPPORTED" >>$log
			n_unsupported=`expr $n_unsupported + 1`;;
		$exit_trap)
			echo                              "KILLED"
			printf -- "$account_lgfmt" "$tst" "KILLED" >>$log
			n_trap=`expr $n_trap + 1`
			break;;
		*)
			echo                              "exit($exit_code)"
			printf -- "$account_lgfmt" "$tst" "exit($exit_code)" >>$log
			n_trap=`expr $n_trap + 1`
			break;;
		esac
	else
			echo                              "SKIPPED"
			printf -- "$account_lgfmt" "$tst" "SKIPPED" >>$log
			n_untested=`expr $n_untested + 1`
	fi

	date +'@@~  end  at %s - %Y-%m-%d %H:%M:%S' >>$log
	echo $account_fin >>$log
	
done < $schedule

echo ""
echo "Total test: `expr $n_pass + $n_fail`"
echo "  success            : $n_pass"
echo "  failure            : $n_fail"

if [ $n_xpass -gt 0 ]; then
echo "  unexpected success : $n_xpass"
fi
if [ $n_xfail -gt 0 ]; then
echo "  expected failure   : $n_xfail"
fi
if [ $n_unresolved -gt 0 ]; then
echo "  unresolved         : $n_unresolved"
fi
if [ $n_untested -gt 0 ]; then
echo "  untested           : $n_untested"
fi
if [ $n_unsupported -gt 0 ]; then
echo "  unsupported        : $n_unsupported"
fi

case $n_trap in 0) :;; *) exit $exit_trap;; esac
[ $n_fail -eq 0 -a $n_unresolved -eq 0 ]
