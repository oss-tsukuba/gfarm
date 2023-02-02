#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

test_gfmkdir_M()
{
    mtime="$1"
    expect="$2"

    ok=0
    if gfmkdir -M $mtime $gftmp &&
       [ x"`gfls -ldT $gftmp | awk '{ print $6,$7,$8,$9 }'`" = x"$expect" ]
    then
	ok=1
    fi
    gfrmdir $gftmp
    if [ $ok -eq 0 ]; then
        exit $exit_fail
    fi
}

# $ touch -d @1234567890 /tmp/aaa; TZ=UTC ls -l --full-time /tmp/aaa
# -rw-r--r-- 1 user1 user1 0 2009-02-13 23:31:30.000000000 +0000 /tmp/aaa
#
# $ gfmkdir -M 1234567890 /tmp/AAA; TZ=UTC gfls -ldT /tmp/AAA
# drwxr-xr-x 2 user1    gfarmadm          0 Feb 13 23:31:30 2009 /tmp/AAA

TZ=UTC
export TZ
test_gfmkdir_M 1234567890 "Feb 13 23:31:30 2009"

exit $exit_pass
