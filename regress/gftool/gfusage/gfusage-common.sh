#!/bin/sh

# XXX FIXME: runnig this script simultaneously is not safe

GFPREP=$regress/bin/gfprep_for_test

cleanup() {
    gfrm -rf $gftmp
}

trap 'cleanup; exit $exit_trap' $trap_sigs

error() {
    echo >&2 "ERROR: $1"
    cleanup
    exit $exit_fail
}

setup() {
    gfmkdir $gftmp || error "gfmkdir"
    gfncopy -s 1 $gftmp > /dev/null 2>&1 # ignore

    FILE=$gftmp/file
    GF_URL=gfarm://${FILE}

    gfreg $data/1byte $FILE || error "gfreg"

    lsl=`gfls -l $FILE` || error "gfls -l"

    user=`echo $lsl | awk '{print $3}'`
    group=`echo $lsl | awk '{print $4}'`

    qu=`get_quota_user "$user"`
    qg=`get_quota_group "$group"`

    if [ -z "$qu" -o -z "$qg" ]; then
        if [ -z "$qu" ]; then
            echo "quota for $user is not enabled"
        fi
        if [ -z "$qg" ]; then
            echo "quota for $group is not enabled"
        fi
        cleanup
        exit $exit_unsupported
    fi
}

get_quota_user() {
    gfquota -u "$1" || error "gfquota -u $1"
}

get_quota_group() {
    gfquota -g "$1" || error "gfquota -g $1"
}

get_quota_val() {
    echo "$1" | egrep "^$2[[:space:]]+:" | awk '{print $3}'
}

get_usage_user() {
    gfusage "$1" | tail -1
}

get_usage_group() {
    gfusage -g "$1" | tail -1
}

get_usage_val() {
    echo "$1" | awk '{print $3" "$4" "$5" "$6}'
}

usage_cmp() {
    echo "-- usage for $5 --"
    echo $1 : $3
    echo $2 : $4

    if [ "$1" != "$2" ]; then
        error "different usage for $5"
    fi
}

gen_expect() {
    _num=$1
    _ncopy=$2
    _size=$3

    _qufs=`echo $4 | cut -d ' ' -f 1`
    _qufn=`echo $4 | cut -d ' ' -f 2`
    _qups=`echo $4 | cut -d ' ' -f 3`
    _qupn=`echo $4 | cut -d ' ' -f 4`
    _qgfs=`echo $4 | cut -d ' ' -f 5`
    _qgfn=`echo $4 | cut -d ' ' -f 6`
    _qgps=`echo $4 | cut -d ' ' -f 7`
    _qgpn=`echo $4 | cut -d ' ' -f 8`

    _qufs1=`expr $_qufs + $_size \* $_num`
    _qufn1=`expr $_qufn + $_num`
    _qups1=`expr $_qups + $_size \* $_ncopy \* $_num`
    _qupn1=`expr $_qupn + $_ncopy \* $_num`
    _qgfs1=`expr $_qgfs + $_size \* $_num`
    _qgfn1=`expr $_qgfn + $_num`
    _qgps1=`expr $_qgps + $_size \* $_ncopy \* $_num`
    _qgpn1=`expr $_qgpn + $_ncopy \* $_num`

    echo "$_qufs1 $_qufn1 $_qups1 $_qupn1 $_qgfs1 $_qgfn1 $_qgps1 $_qgpn1"
}

get_quota_all() {
    qu=`get_quota_user "$user"`
    qg=`get_quota_group "$group"`

    qufs=`get_quota_val "$qu" FileSpace`
    qufn=`get_quota_val "$qu" FileNum`
    qups=`get_quota_val "$qu" PhysicalSpace`
    qupn=`get_quota_val "$qu" PhysicalNum`
    qgfs=`get_quota_val "$qg" FileSpace`
    qgfn=`get_quota_val "$qg" FileNum`
    qgps=`get_quota_val "$qg" PhysicalSpace`
    qgpn=`get_quota_val "$qg" PhysicalNum`

    echo "$qufs $qufn $qups $qupn $qgfs $qgfn $qgps $qgpn"
}

get_usage_all() {
    uusage=`get_usage_user "$user"`
    gusage=`get_usage_group "$group"`

    uval=`get_usage_val "$uusage"`
    gval=`get_usage_val "$gusage"`

    echo "$uval $gval"
}
