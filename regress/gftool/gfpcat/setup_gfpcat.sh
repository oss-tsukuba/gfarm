gfile1=gfarm:$gftmp/file1
gfile2=gfarm:$gftmp/file2
gfile_zero=gfarm:$gftmp/0byte
gfile_out=gfarm:$gftmp/OUTPUT
lfile1=$localtmp/tmpfile1
lfile2=$localtmp/tmpfile2
lfile_zero=$data/0byte
lfile_out=$localtmp/OUTPUT

RANDF=/dev/urandom
RAND_NUM=`awk "BEGIN{srand();print int(1024 * rand()) + 1;}"`
GFPCAT="gfpcat -c"

clean_test() {
    rm -rf $localtmp
    gfrm -rf $gftmp
}

trap 'clean_test; exit $exit_trap' $trap_sigs

create_rand_file()
{
    FILE=$1
    SIZE=`expr 1024 \* 1024 + $RAND_NUM`
    dd if=$RANDF of=$FILE bs=1M count=$SIZE iflag=count_bytes 2> /dev/null
}

setup_test()
{
    if mkdir $localtmp &&
        gfmkdir $gftmp; then
        :
    else
        exit $exit_fail
    fi

    create_rand_file $lfile1
    create_rand_file $lfile2
    gfreg $lfile1 $gfile1
    gfreg $lfile2 $gfile2
    gfreg $lfile_zero $gfile_zero
}
