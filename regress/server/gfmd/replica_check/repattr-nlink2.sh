#!/bin/sh

base=`dirname $0`
. ${base}/replica_check-common.sh
replica_check_setup_test

# setup: create a nlink=2 file
hardlink $tmpf ${tmpf}.lnk
set_repattr $NCOPY1 $gftmp
wait_for_rep $NCOPY1 $tmpf false "#1 setup nlink=2"  ### not timeout

# set repattr to the parent dirctory: do not decrease replicas
set_repattr $NCOPY2 $gftmp
wait_for_rep $NCOPY2 $tmpf true "#2 parent dir"  ### timeout

# set repattr to the file: can decrease replicas
set_repattr $NCOPY2 $tmpf
wait_for_rep $NCOPY2 $tmpf false "#3 file" ### not timeout

exit_code=$exit_pass
