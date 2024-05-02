#!/bin/sh

LINECOUNTS=$(cat /proc/self/cgroup | wc -l)

if [ $LINECOUNTS -eq 1 ]; then
  echo "This system is using cgroup v2."
  exit 0
else
  echo "This system is using cgroup v1."
  exit 1
fi
