#!/bin/sh

. ./regress.conf
. ${testbase}/gfusage-common.sh

test_chown() {
    ncopy=$1

    gfrm -f $FILE || error "gfrm -f"

    allquota1=`get_quota_all`
    allusage1=`get_usage_all`
    usage_cmp "$allquota1" "$allusage1" gfquota1 gfusage1 for_chown

    echo "***** gfreg *****"
    gfreg $data/1byte $FILE || error "gfreg"
    echo "***** gfrep -N $ncopy *****"
    gfrep -N $ncopy $FILE > /dev/null || error "gfrep -N $ncopy"

    allquota2=`get_quota_all`
    allusage2=`get_usage_all`
    usage_cmp "$allquota2" "$allusage2" gfquota2 gfusage2 for_chown

    allexpect2=`gen_expect 1 $ncopy 1 "$allquota1"`
    usage_cmp "$allquota2" "$allexpect2" gfquota2 expected2 for_chown

    user2=`gfuser | grep -v "$user" | head -1`
    group2=`gfgroup | grep -v "$group" | head -1`

    echo "***** gfchown: $user:$group -> $user2:$group2 *****"
    gfchown "${user2}:${group2}" $FILE

    allquota3=`get_quota_all`
    allusage3=`get_usage_all`
    usage_cmp "$allquota3" "$allusage3" gfquota3 gfusage3 for_chown

    allexpect3=`gen_expect -1 $ncopy 1 "$allquota2"`
    usage_cmp "$allquota3" "$allexpect3" gfquota3 expected3 for_chown

    echo "***** gfchown: $user2:$group2 -> $user:$group *****"
    gfchown "${user}:${group}" $FILE

    allquota4=`get_quota_all`
    allusage4=`get_usage_all`
    usage_cmp "$allquota4" "$allusage4" gfquota4 gfusage4 for_chown

    allexpect4=`gen_expect 1 $ncopy 1 "$allquota3"`
    usage_cmp "$allquota4" "$allexpect4" gfquota4 expected4 for_chown
}

$regress/bin/am_I_gfarmroot || exit $exit_unsupported

setup

n=`gfsched | wc -l`
[ $n -le 0 ] && error "no filesystem node"
test_chown $n

cleanup
exit $exit_pass
