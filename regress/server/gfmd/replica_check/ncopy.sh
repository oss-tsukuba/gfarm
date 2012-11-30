#!/bin/sh

# this test may fail, if number of files which don't have enough
# replicas are too many.

NCOPY=2
WAIT_TIME_LIMIT=20  # sec.

. ./regress.conf

[ `gfsched -w | wc -l` -ge $NCOPY ] || exit $exit_unsupported

tmpf=$gftmp/foo
statf=$localtmp

clean_test() {
  gfrm -rf $gftmp
  rm -f $statf
}

trap 'clean_test; exit $exit_trap' $trap_sigs

set_ncopy() {
  if gfncopy -s $1 $2; then
    :
   else
    echo failed gfxattr -s
    clean_test
    exit $exit_fail
  fi
}

setup_test() {
  if gfmkdir $gftmp &&
    set_ncopy 1 $gftmp &&  ### avoid looking parent gfarm.ncopy
    gfreg $data/1byte $tmpf; then
    :
  else
    exit $exit_fail
  fi
}

wait_replication() {
  WAIT_TIME=0
  while
    if gfstat $tmpf > $statf 2>&1; then
      # cat $statf  ### for debug
      :
    else
      echo failed gfstat
      cat $statf
      clean_test
      exit $exit_fail
    fi
    if [ `awk '/Ncopy/{print $NF}' $statf` -eq $1 ]; then
      exit_code=$exit_pass
      false # exit from this loop
    else
      true
    fi
  do
    WAIT_TIME=`expr $WAIT_TIME + 1`
    if [ $WAIT_TIME -gt $WAIT_TIME_LIMIT ]; then
      echo replica_check timeout
      clean_test
      exit $exit_fail
    fi
    sleep 1
  done
}

clean_test
setup_test

set_ncopy $NCOPY $gftmp
wait_replication $NCOPY

clean_test

exit $exit_code
