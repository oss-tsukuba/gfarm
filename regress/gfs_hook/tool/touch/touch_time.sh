#!/bin/sh

. ./regress.conf

trap 'rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp &&
   touch $hooktmp/AXAXAX &&
   sleep 1 &&
   touch $hooktmp/BZBZBZ &&
   sleep 1 &&
   touch $hooktmp/AXAXAX &&
   ls -t $hooktmp/AXAXAX $hooktmp/BZBZBZ | head -1 | grep AXAXAX >/dev/null
then
	exit_code=$exit_pass
fi

rm -rf $hooktmp

case $REGRESS_HOOK_MODE in
gfs_hook)
    case `gfarm.arch.guess` in
    i386-fedora[5-9]-linux|i386-fedora[1-9][0-9]-linux)
	# documented in README.hook.*, its cause hasn't been investigated yet.
	case $exit_code in
	$exit_pass)	exit_code=$exit_xpass;;
	$exit_fail)	exit_code=$exit_xfail;;
	esac;;
    esac;;
esac

exit $exit_code
