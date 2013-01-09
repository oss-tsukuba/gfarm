#!/bin/sh

. ./regress.conf

dir=$gftmp

clean_test() {
	gfrmdir $dir 2>/dev/null
}

me=`gfwhoami`
echo me : $me
gfmkdir $dir
out_ls=`gfls -ld $dir`
echo dir : $out_ls
owner=`echo \"$out_ls\" | awk '{print $3}'`
echo owner : $owner

if [ "$me" != "$owner" ]; then
	echo me is not owner
	clean_test
	exit $exit_fail
fi

clean_test
exit $exit_pass
