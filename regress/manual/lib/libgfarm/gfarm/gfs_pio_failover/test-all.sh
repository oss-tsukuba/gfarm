#!/bin/sh

run_test() {
	_script=$1
	echo "================================"
	$_script
	if [ "$SLEEP" != "" ]; then
		sleep $SLEEP
	fi
}

run_test ./test-sched-read.sh
run_test ./test-sched-create-write.sh
run_test ./test-sched-open-write.sh
run_test ./test-read.sh
run_test ./test-read-stat.sh
run_test ./test-getc.sh
run_test ./test-seek.sh
run_test ./test-seek-dirty.sh
run_test ./test-write.sh
run_test ./test-write-stat.sh
run_test ./test-putc.sh
run_test ./test-truncate.sh
run_test ./test-flush.sh
run_test ./test-sync.sh
run_test ./test-datasync.sh
