#!/bin/sh

. ./regress.conf

gunzipped_file=$localtop/RT_gzip_list.$$

trap 'rm -f $gunzipped_file; gfrm -f $hooktmp;  exit $exit_trap' $trap_sigs

archive="gftest-0.0.tar.gz"

if gzip -cd $data/$archive >$gunzipped_file &&
   cp $data/$archive $hooktmp".gz" &&
   gzip -d $hooktmp".gz" && [ -s $hooktmp ] &&
   cmp -s $gunzipped_file $hooktmp; then
	exit_code=$exit_pass
fi

rm -f $gunzipped_file
gfrm -f $hooktmp
exit $exit_code
