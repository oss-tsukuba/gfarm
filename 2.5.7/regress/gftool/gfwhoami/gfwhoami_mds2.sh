#!/bin/sh

. ./regress.conf

dir=gfarm://$GFARM_TEST_MDS2$gftmp

clean_test() {
	gfrmdir $dir 2>/dev/null
}

if [ "$GFARM_TEST_MDS2" = "" ]; then
	echo GFARM_TEST_MDS2 is not set
	exit $exit_unsupported
fi

me=`gfwhoami -P $`
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
