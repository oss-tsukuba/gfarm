#!/bin/sh

. ./regress.conf

testdir=${gftmp}_dir
testfile=${gftmp}_file
tmpuser1=tmpusr1-$(hostname)-$$
tmpgroup1=tmpgrp1-$(hostname)-$$
tmpgroup2=tmpgrp2-$(hostname)-$$
user=$(gfwhoami)

gfroot_enable() {
    if ! $test_for_gfarmroot; then
        return  # no change
    fi
    if $regress/bin/am_I_gfarmroot; then :; else
        gfgroup -a -m gfarmroot $user || error "gfgroup -a -m"
    fi
}

gfroot_disable() {
    if ! $test_for_gfarmroot; then
        return  # no change
    fi
    if $regress/bin/am_I_gfarmroot; then
        gfgroup -r -m gfarmroot $user || error "gfgroup -r m"
    fi
}

cleanup() {
    gfroot_enable
    gfrm -f $testfile
    gfrmdir $testdir
    if $test_for_gfarmroot; then
        gfuser -d $tmpuser1
        gfgroup -d $tmpgroup1
        gfgroup -d $tmpgroup2
    fi
}

trap 'cleanup; exit $exit_trap' $trap_sigs

error()
{
    msg=$1
    echo "error: $msg"
    cleanup
    exit $exit_code
}

test_gfls_e1() {
    perm=$1
    path=$2

    result=$(gfls -e1d $path | awk '{print $1}')
    [ "$perm" = "$result" ] || error "-e1,$perm,$result,$path"
}

test_gfls_eC() {
    perm=$1
    path=$2

    result=$(gfls -eCd $path | awk '{print $1}')
    [ "$perm" = "$result" ] || error "-eC,$perm,$result,$path"
}

test_gfxattr() {
    perm=$1
    path=$2

    val=$(gfxattr -g $path gfarm.effective_perm | od -An -td1)
    if [ $(($val & 4)) -ne 0 ]; then
        R_OK='r'
    else
        R_OK='-'
    fi
    if [ $(($val & 2)) -ne 0 ]; then
        W_OK='w'
    else
        W_OK='-'
    fi
    if [ $(($val & 1)) -ne 0 ]; then
        X_OK='x'
    else
        X_OK='-'
    fi
    result=${R_OK}${W_OK}${X_OK}
    [ "$perm" = "$result" ] || error "gfxattr,$perm,$result,$path"
}

test_compare() {
    perm=$1

    test_gfls_e1 $perm $testdir
    test_gfls_e1 $perm $testfile
    test_gfls_eC $perm $testdir
    test_gfls_eC $perm $testfile
    test_gfxattr $perm $testdir
    test_gfxattr $perm $testfile
}

test_ep() {
    mode=$1
    perm=$2

    gfroot_enable
    gfchmod $mode $testdir $testfile || error "gfchmod,$mode,$perm"
    gfroot_disable
    test_compare $perm
}

test_ep_acl() {
    acl_spec=$1
    perm=$2

    gfroot_enable
    gfsetfacl -b -m $acl_spec $testdir $testfile || \
        error "gfsetfacl,$acl_spec,$perm"
    # gfgetfacl $testdir $testfile
    gfroot_disable
    test_compare $perm
}

if $test_for_gfarmroot; then
    gfuser -c $tmpuser1 "tmp user1" "/home/$tmpuser" "" || error "gfuser -c"
    gfgroup -c $tmpgroup1 || error "gfgroup -c"
    gfgroup -c $tmpgroup2 || error "gfgroup -c"
fi
gfmkdir $testdir || error "gfmkdir"
gfreg $data/1byte $testfile || error "gfreg"
