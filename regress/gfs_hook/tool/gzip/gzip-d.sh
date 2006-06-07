#!/bin/sh

. ./regress.conf

gunzipped_file=$localtop/RT_gzip_list.$$

trap 'rm -f $gunzipped_file; gfrm -f $gftmp;  exit $exit_trap' $trap_sigs

archive="gftest-0.0.tar.gz"

if gzip -cd $data/$archive >$gunzipped_file &&
   cp $data/$archive $gftmp".gz" &&
   gzip -d $gftmp".gz" && [ -s $gftmp ] &&
   cmp -s $gunzipped_file $gftmp; then
	exit_code=$exit_pass
fi

rm -f $gunzipped_file
gfrm -f $gftmp
exit $exit_code
