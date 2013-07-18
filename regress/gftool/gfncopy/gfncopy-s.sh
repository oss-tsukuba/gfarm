#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

if gfmkdir $gftmp; then
    :
else
    exit $exit_fail
fi

EXPECT_OK=0
EXPECT_EINVAL=1
EXPECT_ERANGE=2

is_not_expected() {
    if [ $1 -eq $EXPECT_EINVAL -a "$2" = "invalid argument" ]; then
        return 1
    elif [ $1 -eq $EXPECT_ERANGE -a "$2" = "result out of range" ]; then
        return 1
    else
        return 0 # is not expected
    fi
}

test_gfncopy() {
    VAL=$1
    EXPECT=$2
    msg=`gfncopy -s "$VAL" $gftmp 2>&1`
    if [ $? -eq 0 ]; then
        if [ $EXPECT -ne $EXPECT_OK ]; then
            exit_code=$exit_fail
            echo >&2 "gfncopy -s \"$VAL\" should fail"
        fi
    else
        if [ $EXPECT -eq $EXPECT_OK ]; then
            exit_code=$exit_fail
            echo >&2 "gfncopy -s \"$VAL\" failed: $msg"
        elif is_not_expected $EXPECT "$msg"; then
            exit_code=$exit_fail
            echo >&2 "gfncopy -s \"$VAL\", unexpected error: $msg"
        fi
    fi
}

test_gfxattr_ncopy() {
    VAL=$1
    EXPECT=$2
    msg=`printf '%s' "$VAL" | gfxattr -s $gftmp gfarm.ncopy 2>&1`
    if [ $? -eq 0 ]; then
        if [ $EXPECT -ne $EXPECT_OK ]; then
            exit_code=$exit_fail
            echo >&2 "printf '%s' \"$VAL\" | gfxattr -s should fail"
        fi
    else
        if [ $EXPECT -eq $EXPECT_OK ]; then
            exit_code=$exit_fail
            echo >&2 "printf '%s' \"$VAL\" | gfxattr -s failed: $msg"
        elif is_not_expected $EXPECT "$msg"; then
            exit_code=$exit_fail
            echo >&2 "printf '%s' \"$VAL\" | gfxattr -s, unexpected error: $msg"
        fi
    fi
}

exit_code=$exit_pass

test_gfncopy " 1" $EXPECT_OK
test_gfncopy "1 " $EXPECT_OK
test_gfncopy "-1" $EXPECT_EINVAL
test_gfncopy "2147483647" $EXPECT_OK
test_gfncopy "2147483648" $EXPECT_ERANGE
test_gfncopy "4294967295" $EXPECT_ERANGE
test_gfncopy "4294967296" $EXPECT_ERANGE

test_gfxattr_ncopy " 1" $EXPECT_EINVAL
test_gfxattr_ncopy "1 " $EXPECT_EINVAL
test_gfxattr_ncopy "-1" $EXPECT_EINVAL
test_gfxattr_ncopy "2147483647" $EXPECT_OK
test_gfxattr_ncopy "2147483648" $EXPECT_ERANGE
test_gfxattr_ncopy "4294967295" $EXPECT_ERANGE
test_gfxattr_ncopy "4294967296" $EXPECT_ERANGE

gfrmdir $gftmp
exit $exit_code
