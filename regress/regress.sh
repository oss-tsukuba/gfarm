#!/bin/sh

. ./account.sh

log=log
rm -f $log
exec >$log

case $# in
0)	schedule=./schedule;;
*)	schedule=$*;;
esac

clear_counters
fmt_init
killed=0

while read line; do
	set x $line
	shift
	case $# in 0) continue;; esac
	case $1 in '#'*) continue;; esac

	tst=$1
	if [ -f $tst ]; then
		print_header

		# XXX FIXME: Solaris 9 /bin/sh dumps core with sh/test-d.sh
		$shell $tst </dev/null 2>&1

		eval_result $?
		exit_code=$?
		if [ $exit_code -eq $exit_trap ]; then
			killed=1
			break
		fi
		print_footer
	else
		treat_as_untested
	fi
done < $schedule

print_summary >&2

if [ $killed -eq 1 ]; then exit $exit_trap; fi
exitcode_by_summary
