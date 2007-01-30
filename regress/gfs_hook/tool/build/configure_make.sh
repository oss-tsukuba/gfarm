#!/bin/sh

. ./regress.conf

trap 'cd; rm -rf $hooktmp; exit $exit_trap' $trap_sigs

if ! gfhost -M `hostname` >/dev/null
then
	# configure does not work on non-filesystem node
	# this problem in not documented yet    
	exit $exit_xfail
fi    

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

case $REGRESS_HOOK_MODE in
gfs_hook)
	case `gfarm.arch.guess` in
	*-*-freebsd*|*-*-solaris*)
		# documented in README.hook.*.
		# FreeBSD: the cause is not investigated yet.
		# Solaris: due to access(2) hook problem?
		case $exit_code in
		$exit_pass)	exit_code=$exit_xpass;;
		$exit_fail)	exit_code=$exit_xfail;;
		esac;;
	*)	# documented in README.hook.*.
		case $REGRESS_AUTH in
		gsi)	case $exit_code in
			$exit_pass)	exit_code=$exit_xpass;;
			$exit_fail)	exit_code=$exit_xfail;;
			esac;;
		esac;;
	esac;;
gfarmfs)
	case $REGRESS_FUSE_OPT in
	*direct_io*)	case $exit_code in
			$exit_pass)	exit_code=$exit_xpass;;
			$exit_fail)	exit_code=$exit_xfail;;
			esac;;
	esac;;
esac

exit $exit_code
