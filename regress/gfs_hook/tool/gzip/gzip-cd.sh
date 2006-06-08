#!/bin/sh

. ./regress.conf

gunzipped_file=$localtop/RT_gzip_d_list.$$

trap 'rm -f $gunzipped_file $hooktmp".gz";  exit $exit_trap' $trap_sigs

archive="gftest-0.0.tar.gz"

if gzip -cd $data/$archive >$gunzipped_file &&
   cp $data/$archive $hooktmp".gz" &&
   gzip -cd $hooktmp".gz" | cmp -s $gunzipped_file -; then
	exit_code=$exit_pass
fi

rm -f $gunzipped_file $hooktmp".gz"
exit $exit_code
