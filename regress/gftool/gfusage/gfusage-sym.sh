#!/bin/sh

. ./regress.conf
. ${testbase}/gfusage-common.sh

test_sym() {
    gfrm -rf $FILE || error "gfrm -rf"

    allquota1=`get_quota_all`
    allusage1=`get_usage_all`
    usage_cmp "$allquota1" "$allusage1" gfquota1 gfusage1 for_sym

    echo "***** gfln -s *****"
    gfln -s $gftmp $FILE || error "gfln -s"

    allquota2=`get_quota_all`
    allusage2=`get_usage_all`
    usage_cmp "$allquota2" "$allusage2" gfquota2 gfusage2 for_sym

    allexpect2=`gen_expect 1 0 0 "$allquota1"`
    usage_cmp "$allquota2" "$allexpect2" gfquota2 expected2 for_sym

    echo "***** gfrm *****"
    gfrm $FILE || error "gfrm"

    allquota3=`get_quota_all`
    allusage3=`get_usage_all`
    usage_cmp "$allquota3" "$allusage3" gfquota3 gfusage3 for_sym

    allexpect3=`gen_expect -1 0 0 "$allquota2"`
    usage_cmp "$allquota3" "$allexpect3" gfquota3 expected3 for_sym
}

setup
test_sym

cleanup
exit $exit_pass
