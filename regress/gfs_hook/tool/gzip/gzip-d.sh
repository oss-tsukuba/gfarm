#!/bin/sh

. ./regress.conf

gunzipped_file=$localtop/RT_gzip_d_list.$$

trap 'rm -f $gunzipped_file $hooktmp;  exit $exit_trap' $trap_sigs

archive="gftest-0.0.tar.gz"

if gzip -cd $data/$archive >$gunzipped_file &&
   cp $data/$archive $hooktmp".gz" &&
   gzip -d $hooktmp".gz" && [ -s $hooktmp ] &&
   cmp -s $gunzipped_file $hooktmp; then
	exit_code=$exit_pass
fi

rm -f $gunzipped_file $hooktmp

case `gfarm.arch.guess` in
*-*-netbsd*)
	# documented in README.hook.*, due to chflags(2) hook problem?
	case $exit_code in
	$exit_pass)	exit_code=$exit_xpass;;
	$exit_fail)	exit_code=$exit_xfail;;
	esac;;
esac
exit $exit_code
