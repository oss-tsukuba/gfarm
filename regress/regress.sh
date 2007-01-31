#!/bin/sh

. ./regress.conf
. $regress/account.sh

do_init=0
do_test=0
do_summary=0
while	case "$1" in
	-i)	do_init=1; true;;
	-t)	do_test=1; true;;
	-s)	do_summary=1; true;;
	-*)
		echo "$0: unknown option $1" >&2
		exit 1;;
	*)
		false;;
	esac
do
	shift
done

case $# in
0)	schedule=$regress/schedule;;
*)	schedule=$*;;
esac

case ${do_init}${do_test}${do_summary} in
000)	do_init=1
	do_test=1
	do_summary=1;;
esac

case $do_init in
1)	rm -f $log;;
esac

exec >>$log

killed=0

case $do_test in
1)
	while read line; do
		set x $line
		shift
		case $# in 0) continue;; esac
		case $1 in '#'*) continue;; esac

		tst=$1
		print_header
		if [ -f $regress/$tst ]; then
			# XXX FIXME: Solaris 9 /bin/sh dumps core with sh/test-d.sh
			$shell $regress/$tst </dev/null 2>&1

			eval_result $?
			exit_code=$?
			if [ $exit_code -eq $exit_trap ]; then
				killed=1
				break
			fi
		else
			treat_as_untested
		fi
		print_footer
	done < $schedule
	;;
esac

case $do_summary in
1)	print_summary $log >&2;;
*)	print_summary $log >/dev/null;; # to set $exit_code
esac

if [ $killed -eq 1 ]; then exit $exit_trap; fi
exitcode_by_summary
