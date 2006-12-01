#!/bin/sh

. ./regress.conf

localtmp=$localtop/"`echo $0 | sed s:/:_:g`".$$

trap 'rm -rf $localtmp $hooktmp; exit $exit_trap' \
     $trap_sigs

if mkdir $localtmp && mkdir $hooktmp &&
   gzip -cd $data/gftest-0.0.tar.gz | ( cd $localtmp && pax -r ) &&
   gzip -cd $data/gftest-0.0.tar.gz | ( cd $hooktmp  && pax -r ) &&
   diff -r $localtmp/gftest-0.0 $hooktmp/gftest-0.0 >/dev/null
then
	exit_code=$exit_pass
fi

rm -rf $localtmp $hooktmp

case `gfarm.arch.guess` in
*-*-solaris*)
	# documented in README.hook.*, due to fsat(2) hook problem.
	case $exit_code in
	$exit_pass)	exit_code=$exit_xpass;;
	$exit_fail)	exit_code=$exit_xfail;;
	esac;;
esac
exit $exit_code
