#!/bin/sh

. ./regress.conf

trap 'cd; rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if mkdir $hooktmp &&
   gzip -cd $data/gftest-0.0.tar.gz | ( cd $hooktmp && pax -r ) &&
   cd $hooktmp/gftest-0.0 &&
   env CONFIG_SHELL=`which $shell` $shell ./configure &&
   make
then
	exit_code=$exit_pass
fi

cd
rm -rf $hooktmp

case `gfarm.arch.guess` in
*-*-freebsd*|*-*-solaris*)
	# documented in README.hook.*.
	# FreeBSD: the cause is not investigated yet.
	# Solaris: due to access(2) hook problem?
	case $exit_code in
	$exit_pass)	exit_code=$exit_xpass;;
	$exit_fail)	exit_code=$exit_xfail;;
	esac;;
esac
exit $exit_code
