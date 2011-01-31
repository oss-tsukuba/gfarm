#!/bin/sh

. ./regress.conf

tmpj=$localtmp

clean() {
	rm -f $tmpj
}

clean_fail() {
	echo $*
	clean
	exit $exit_code
}

if $testbin/db_journal_test -o $tmpj; then :
else
	exit_code=$?
	clean_fail "failed db_journal_test -o"
fi

trap 'clean; exit $exit_trap' $trap_sigs

clean
exit $exit_pass
