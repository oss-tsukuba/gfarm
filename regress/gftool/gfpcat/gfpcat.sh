#!/bin/sh

set -eu

. ./regress.conf

BASEDIR=`dirname $0`
. ${BASEDIR}/setup_gfpcat.sh

setup_test

test_write_to_gfarm_j1()
{
    gfrm -f $gfile_out
    # with -t
    $GFPCAT -t -j 1 -o $gfile_out $gfile2 $lfile2 $gfile1 file:$lfile1
}

test_write_to_local()
{
    rm -f $lfile_out
    $GFPCAT -o $lfile_out $lfile1 file:$lfile2 $gfile1 $gfile2
}

test_write_to_gfarm()
{
    gfrm -f $gfile_out
    $GFPCAT -o $gfile_out $gfile2 $gfile1 $lfile2 file:$lfile1
}

test_write_to_local_0byte()
{
    rm -f $lfile_out
    $GFPCAT -o $lfile_out $gfile_zero $lfile_zero
}

test_write_to_gfarm_0byte()
{
    gfrm -f $gfile_out
    $GFPCAT -o $gfile_out $gfile_zero $lfile_zero
}

test_overwrite_to_local()
{
    rm -f $lfile_out
    $GFPCAT -o $lfile_out $lfile1 $gfile1
    if $GFPCAT -o $lfile_out $lfile1 $gfile1 2> /dev/null; then
        exit $exit_fail
    fi
    $GFPCAT -f -o $lfile_out $lfile1 $gfile1
}

test_overwrite_to_gfarm()
{
    gfrm -f $gfile_out
    $GFPCAT -o $gfile_out $lfile1
    if $GFPCAT -o $gfile_out $lfile1 2> /dev/null; then
        exit $exit_fail
    fi
    $GFPCAT -f -o $gfile_out $lfile1
}

test_same_locale_file()
{
    if $GFPCAT -f -o file:$lfile1 $lfile1 2> /dev/null; then
        exit $exit_fail
    fi
}

test_same_gfarm_file()
{
    if $GFPCAT -f -o $gfile1 $gfile1 2> /dev/null; then
        exit $exit_fail
    fi
}

test_inputlist_from_file()
{
    gfrm -f $gfile_out
    LIST=$lfile_out
    echo $lfile1 > $LIST
    echo file:$lfile2 >> $LIST
    echo $gfile1 >> $LIST
    echo $gfile2 >> $LIST
    $GFPCAT -o $gfile_out -i $LIST
}

test_inputlist_from_stdin()
{
    gfrm -f $gfile_out
    cat <<EOF | $GFPCAT -o $gfile_out -i -
$lfile1
file:$lfile2
$gfile1
$gfile2
EOF
}

test_write_to_gfarm_j1
test_write_to_local
test_write_to_gfarm
test_write_to_local_0byte
test_write_to_gfarm_0byte
test_overwrite_to_local
test_overwrite_to_gfarm
test_same_locale_file
test_same_gfarm_file
test_inputlist_from_file
test_inputlist_from_stdin

clean_test
exit $exit_pass
