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

if $testbin/db_journal_test -w $tmpj; then :
else
	exit_code=$?
	clean_fail "failed db_journal_test -w"
fi

trap 'clean; exit $exit_trap' $trap_sigs

clean
exit $exit_pass
