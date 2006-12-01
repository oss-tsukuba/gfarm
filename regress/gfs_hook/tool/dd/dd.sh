#!/bin/sh

. ./regress.conf

trap 'rm -f $hooktmp; exit $exit_trap' $trap_sigs

ulimit -c 0	# do not dump core

if dd if=$data/1byte of=$hooktmp
then
	exit_code=$exit_pass
elif [ $? -eq 139 ]; then # 139 = 128 + 11 (signal 11 = Segmentation fault)
	# known bug on Linux
	case `uname` in
	Linux)	exit_code=$exit_xfail;;
	esac
fi

rm -f $hooktmp

case `gfarm.arch.guess` in
i386-fedora5-linux)
	# documented in README.hook.*, the cause is not investigated yet.
	case $exit_code in
	$exit_pass)	exit_code=$exit_xpass;;
	$exit_fail)	exit_code=$exit_xfail;;
	esac;;
esac
exit $exit_code
