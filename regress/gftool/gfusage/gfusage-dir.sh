#!/bin/sh

. ./regress.conf
. ${testbase}/gfusage-common.sh

test_dir() {
    gfrm -rf $FILE || error "gfrm -rf"

    allquota1=`get_quota_all`
    allusage1=`get_usage_all`
    usage_cmp "$allquota1" "$allusage1" gfquota1 gfusage1 for_dir

    echo "***** gfmkdir *****"
    gfmkdir $FILE || error "gfmkdir"

    allquota2=`get_quota_all`
    allusage2=`get_usage_all`
    usage_cmp "$allquota2" "$allusage2" gfquota2 gfusage2 for_dir

    allexpect2=`gen_expect 1 0 0 "$allquota1"`
    usage_cmp "$allquota2" "$allexpect2" gfquota2 expected2 for_dir

    echo "***** gfrmdir *****"
    gfrmdir $FILE || error "gfrmdir"

    allquota3=`get_quota_all`
    allusage3=`get_usage_all`
    usage_cmp "$allquota3" "$allusage3" gfquota3 gfusage3 for_dir

    allexpect3=`gen_expect -1 0 0 "$allquota2"`
    usage_cmp "$allquota3" "$allexpect3" gfquota3 expected3 for_dir
}

setup
test_dir

cleanup
exit $exit_pass
