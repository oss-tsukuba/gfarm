#! /bin/sh

full_check=true
[ $# -ge 1 ] && full_check=$1

. ./regress.conf

local_acl=$localtmp/acl
local_acl_orig=$localtmp/acl_orig
gf_file=$gftmp/file
gf_dir=$gftmp/dir

clean_test() {
    rm -f $local_acl
    rm -f $local_acl_orig
    rmdir $localtmp

    gfrm -f $gf_file
    gfrmdir $gf_dir
    gfrmdir $gftmp
}

trap 'clean_test; exit $exit_trap' $trap_sigs

### setup
if mkdir $localtmp &&
    gfmkdir $gftmp &&
    gfmkdir $gf_dir &&
    gfreg $data/1byte $gf_file; then
    :
else
    exit $exit_fail
fi

tmpuser=`gfuser gfarmadm`
if [ x$tmpuser = x ]; then
    tmpuser=`gfuser | head -1`
fi
tmpgroup=`gfgroup gfarmadm`
if [ x$tmpgroup = x ]; then
    tmpgroup=`gfgroup | head -1`
fi

is_dir() {
    if [ $1 = $gftmp -o $1 = $gf_dir ]; then
        return 0
    else
        return 1
    fi
}

check_perm()
{
    file_p=$1
    entry_p=$2
    tq_p=$3
    perm_p=$4
    method_p=$5

    reg_entry=`grep "^$tq_p" $file_p`
    if [ $? -ne 0 ]; then
        echo "fail($method_p, $tq_p): $entry_p does not have $tq_p"
        exit $exit_fail
    fi
    perm_new=""
    if [ `expr "$tq_p" : 'default:'` -eq 8 ]; then
        perm_new=`echo $reg_entry | awk -F '[: ]' '{print $4}'`
    else
        perm_new=`echo $reg_entry | awk -F '[: ]' '{print $3}'`
    fi
    if [ x"$perm_p" != x"$perm_new" ]; then
        echo "fail($method_p, $tq_p): '$perm_p' to '$perm_new'"
        exit $exit_fail
    fi
}

check_effective() {
    file_e=$1
    entry_e=$2
    tq_e=$3
    perm_e=$4

    ef=`grep "^$tq_e" $file_e | awk -F effective: '{print $2}'`
    if [ x"$ef" != x"$perm_e" ]; then
        echo different effective: $perm_e to $ef
        cat $file_e
        exit $exit_fail
    fi
}

test_mM_2()
{
    tq2=$1 # tag and qualifier
    perm2=$2
    entry2=$3
    option2=$4 # -m / -M

    if gfgetfacl $entry2 > $local_acl_orig ;then
        :
    else
        echo fail: gfgetfacl $entry2
        exit $exit_fail
    fi

    if [ x"$option2" = x"-M" ]; then
        if echo "${tq2}${perm2}" | gfsetfacl -M - $entry2; then
            :
        else
            echo "fail: echo ${tq2}${perm2} | gfsetfacl -M - $entry2"
            exit $exit_fail
        fi
    else
        if gfsetfacl -m "${tq2}${perm2}" $entry2; then
            :
        else
            echo fail: gfsetfacl -m "${tq2}${perm2}" $entry2
            exit $exit_fail
        fi
    fi

    if gfgetfacl $entry2 > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry2
        exit $exit_fail
    fi

    check_perm $local_acl $entry2 $tq2 $perm2 $option2

    if gfsetfacl -b -M $local_acl_orig $entry2 ;then
        :
    else
        echo fail: gfsetfacl -M $local_acl_orig $entry2
        exit $exit_fail
    fi
}

test_mM_1() {
    tq1=$1
    perm1=$2
    entry1=$3

    test_mM_2 $tq1 $perm1 $entry1 -m
    test_mM_2 $tq1 $perm1 $entry1 -M
    if is_dir $entry; then
        test_mM_2 "default:$tq1" $perm1 $entry1 -m
        test_mM_2 "default:$tq1" $perm1 $entry1 -M
    fi
}

test_mM_0() {
    tq=$1
    entry=$2

    if $full_check; then
        test_mM_1 $tq --- $entry
        test_mM_1 $tq r-- $entry
        test_mM_1 $tq rw- $entry
        test_mM_1 $tq rwx $entry
        test_mM_1 $tq -w- $entry
        test_mM_1 $tq -wx $entry
        test_mM_1 $tq --x $entry
    fi
    test_mM_1 $tq r-x $entry
}

test_mM() {
    entry=$1

    test_mM_0 "user::" $entry
    test_mM_0 "group::" $entry
    test_mM_0 "other::" $entry
    test_mM_0 "user:${tmpuser}:" $entry
    test_mM_0 "group:${tmpgroup}:" $entry
    test_mM_0 "mask::" $entry
}

test_bk() {
    entry=$1
    option=$2  # -b, -k

    # test chmod
    gfchmod 777 $entry
    if gfgetfacl $entry > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry
        exit $exit_fail
    fi
    check_perm $local_acl $entry "user::" rwx chmod
    check_perm $local_acl $entry "group::" rwx chmod
    check_perm $local_acl $entry "other::" rwx chmod

    mask_acl=""
    if is_dir $entry ; then
        mask_acl="m::---,d:m::---"
    else
        mask_acl="m::---"
    fi
    if gfsetfacl -m $mask_acl $entry; then
        :
    else
        echo fail: gfsetfacl -m $mask_acl $entry
        exit $exit_fail
    fi

    if gfgetfacl $entry > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry
        exit $exit_fail
    fi
    # test effective
    check_effective $local_acl $entry "group::" ---

    if gfsetfacl $option $entry; then
        :
    else
        echo fail: gfsetfacl $option $entry
        exit $exit_fail
    fi
    if gfgetfacl $entry > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry
        exit $exit_fail
    fi
    acl_len=`wc -l $local_acl | awk '{print $1}'`
    if [ x"$option" = x"-b" -a $acl_len -eq 6 ]; then
        :
    elif [ x"$option" = x"-k" -a $acl_len -eq 7 ]; then
        :
    else
        echo "cannot remove ACL (setfacl $option $entry)"
        cat $local_acl
        exit $exit_fail
    fi
    method="setfacl$option"
    check_perm $local_acl $entry "user::" rwx $method
    if [ x"$option" = x"-b" ]; then
        check_perm $local_acl $entry "group::" --- $method
    else
        check_perm $local_acl $entry "group::" rwx $method
    fi

    check_perm $local_acl $entry "other::" rwx $method
}

test_mask() {
    entry=$1

    gfsetfacl -m "m::---" $entry
    if gfsetfacl -m "g::rwx" $entry; then
        :
    else
        echo fail: gfsetfacl
        exit $exit_fail
    fi
    if gfgetfacl $entry > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry
        exit $exit_fail
    fi
    check_perm $local_acl $entry "mask::" rwx 'gfsetfacl [1]'
    gfsetfacl -b $entry

    gfsetfacl -m "m::rwx" $entry
    if gfsetfacl -m "g::rwx,m::---" $entry; then
        :
    else
        echo fail: gfsetfacl
        exit $exit_fail
    fi
    if gfgetfacl $entry > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry
        exit $exit_fail
    fi
    check_perm $local_acl $entry "mask::" --- 'gfsetfacl [2]'
    gfsetfacl -b $entry

    gfsetfacl -m "g::r--" $entry
    if gfsetfacl -m "u:${tmpuser}:--x" $entry; then
        :
    else
        echo fail: gfsetfacl
        exit $exit_fail
    fi
    if gfgetfacl $entry > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry
        exit $exit_fail
    fi
    check_perm $local_acl $entry "mask::" r-x 'gfsetfacl [3]'
    gfsetfacl -b $entry
}

test_n() {
    entry=$1

    gfsetfacl -m "m::---" $entry
    if gfsetfacl -n -m "g::rwx" $entry; then
        :
    else
        echo fail: gfsetfacl with -n option
        exit $exit_fail
    fi
    if gfgetfacl $entry > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry
        exit $exit_fail
    fi
    check_perm $local_acl $entry "mask::" --- 'gfsetfacl -n'
    gfsetfacl -b $entry
}

test_r() {
    entry=$1

    gfsetfacl -m "m::---" $entry
    if gfsetfacl -r -m "g::rwx" $entry; then
        :
    else
        echo fail: gfsetfacl with -r option
        exit $exit_fail
    fi
    if gfgetfacl $entry > $local_acl; then
        :
    else
        echo fail: gfgetfacl $entry
        exit $exit_fail
    fi
    check_perm $local_acl $entry "mask::" rwx 'gfsetfacl -r'
    gfsetfacl -b $entry
}

test_1() {
    entry=$1

    # XXX header comments of gfgetfacl

    # -m or -M
    test_mM $entry

    # -k
    test_bk $entry -k

    # -b
    test_bk $entry -b

    # default : recalculate a mask entry if the mask entry is not given
    test_mask $entry

    # -n : do not recalculate a mask entry
    test_n $entry

    # -r : force to recalculate a mask entry
    test_r $entry
}

test_1 $gf_dir
test_1 $gf_file

# XXX inherit defaul ACL
# XXX unknown user/group name

clean_test
exit $exit_pass
