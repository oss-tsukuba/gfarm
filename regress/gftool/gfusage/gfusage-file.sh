#!/bin/sh

. ./regress.conf
. ${testbase}/gfusage-common.sh

test_file() {
    ncopy=$1

    gfrm -f $FILE || error "gfrm -f"

    allquota1=`get_quota_all`
    allusage1=`get_usage_all`
    usage_cmp "$allquota1" "$allusage1" gfquota1 gfusage1 for_file

    echo "***** gfreg *****"
    gfreg $data/1byte $FILE || error "gfreg"
    echo "***** gfrep -N $ncopy *****"
    gfrep -N $ncopy $FILE > /dev/null || error "gfrep -N $ncopy"

#    gfls -l $FILE
#    gfwhere -la $FILE

    allquota2=`get_quota_all`
    allusage2=`get_usage_all`
    usage_cmp "$allquota2" "$allusage2" gfquota2 gfusage2 for_file

    allexpect2=`gen_expect 1 $ncopy 1 "$allquota1"`
    usage_cmp "$allquota2" "$allexpect2" gfquota2 expected2 for_file

    echo "***** gfrm *****"
    gfrm $FILE || error "gfrm"

    allquota3=`get_quota_all`
    allusage3=`get_usage_all`
    usage_cmp "$allquota3" "$allusage3" gfquota3 gfusage3 for_file

    allexpect3=`gen_expect -1 $ncopy 1 "$allquota2"`
    usage_cmp "$allquota3" "$allexpect3" gfquota3 expected3 for_file
}

setup
test_file 1

n=`gfsched | wc -l`
[ $n -le 0 ] && error "no filesystem node"
test_file $n

cleanup
exit $exit_pass
