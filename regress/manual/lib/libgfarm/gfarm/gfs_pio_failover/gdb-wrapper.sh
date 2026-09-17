#!/bin/sh
REAL_PROG=.libs/gfs_pio_failover_test

echo >&2 "INVOKING GDB for command: ${REAL_PROG} $*"
gdb -q -ex "b main" -ex "run" --args ${REAL_PROG} ${1+"$@"}
