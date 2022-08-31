#!/bin/sh

mount | grep -q "cgroup2 on /sys/fs/cgroup type" > /dev/null 2>&1
