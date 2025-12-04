#!/bin/sh
set -eu
status=1
PROG=$(basename $0)
trap '[ $status = 0 ] && echo Done || echo NG: $PROG; exit $status' 0 1 2 15

: ${ASAN_OPTIONS=halt_on_error=false,log_exe_name=true,log_path=/var/tmp/gfarm.log.asan}
: ${LSAN_OPTIONS=halt_on_error=false,log_exe_name=true,log_path=/var/tmp/gfarm.log.lsan}
: ${UBSAN_OPTIONS=halt_on_error=false,log_exe_name=true,log_path=/var/tmp/gfarm.log.ubsan}
: ${TSAN_OPTIONS=halt_on_error=false,log_exe_name=true,log_path=/var/tmp/gfarm.log.tsan}
export ASAN_OPTIONS LSAN_OPTIONS UBSAN_OPTIONS TSAN_OPTIONS

cmd() {
	echo [$*]
	$*
}

cmd gfdf
cmd gfhost -lv
cmd gfmdhost -l

NOTHEALTHY=0
for h in $(gfmdhost -l | awk '$1 !~ /^\+/ {print $6}')
do
	echo $h: not synchronize, restart
	ssh $h sudo systemctl restart gfmd
	NOTHEALTHY=1
done
[ $NOTHEALTHY = 0 ] || {
	echo not healthy, check again
	sleep 1
	cmd gfmdhost -l
}
status=0
