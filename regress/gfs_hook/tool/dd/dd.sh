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

if [ $exit_code -eq $exit_fail ]; then
    if gfhost -M `hostname` >/dev/null; then
	:
    else
	case `gfarm.arch.guess` in
	# dd issues `Socket operation on non-socket' on non-filesystem node,
	# documented in README.hook.*, its cause hasn't been investigated yet.
	i386-centos4.4-linux)	exit $exit_xfail;;
	esac    
    fi
fi    

case $REGRESS_HOOK_MODE in
gfs_hook)
    case `gfarm.arch.guess` in
    i386-fedora[56]-linux)
	# documented in README.hook.*, its cause hasn't been investigated yet.
	case $exit_code in
	$exit_pass)	exit_code=$exit_xpass;;
	$exit_fail)	exit_code=$exit_xfail;;
	esac;;
    esac;;
esac

exit $exit_code
