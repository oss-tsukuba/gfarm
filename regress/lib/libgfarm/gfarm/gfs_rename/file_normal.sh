#!/bin/sh

. ./regress.conf

trap 'gfrm -f $gftmp_old $gftmp_new; exit $exit_trap' $trap_sigs

# 0x00000401 is GFARM_FILE_WRONLY|GFARM_FILE_TRUNC
if ! $testbin/../gfs_pio_create/create ${gftmp}_old 0x00000401 0644; then
    exit $exit_unsupported
fi

# arguments are <gfarm_url_from> <gfarm_url_to>
$shell $testbase/rename.sh ${gftmp}_old ${gftmp}_new
exit_code=$?

if [ $exit_code = $exit_pass ]; then
    gfrm ${gftmp}_new
else
    gfrm ${gftmp}_old
fi
exit $exit_code
