#!/bin/sh

. ./regress.conf

#trap 'rm -f ${gftmp}*; exit $exit_trap' $trap_sigs

# 0x00000401 is GFARM_FILE_WRONLY|GFARM_FILE_TRUNC
if ! $testbin/../gfs_pio_create/create ${gftmp}_1 0x00000401 0666 ||
   ! $testbin/../gfs_pio_create/create ${gftmp}_2 0x00000401 0666; then
    exit $exit_unsupported
fi

# arguments are <pattern> <paths>
#$shell $testbase/glob.sh ${gftmp}\* '${gftmp}1${gftmp}2'
$shell $testbase/glob.sh ${gftmp}\* "${gftmp}_1${gftmp}_2"
exit_code=$?

gfrm ${gftmp}*
exit $exit_code
