#!/bin/sh

. ./regress.conf

trap 'gfrmdir $gftmp; exit $exit_trap' $trap_sigs

if ! $testbin/../gfs_mkdir/mkdir $gftmp 0755 1>/dev/null 2>&1 ; then
    exit $exit_unsupported
fi

# argument is <gfarm_url>
$shell $testbase/rmdir.sh $gftmp
exit_code=$?

if [ $exit_code != $exit_pass ]; then
    gfrmdir $gftmp
fi
exit $exit_code
