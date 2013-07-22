#!/bin/sh -x

# this test may fail, if number of files which don't have enough
# replicas are too many.

NCOPY=3
WAIT_TIME_LIMIT=20  # sec.
hostgroupfile=/tmp/.hostgroup.$$

. ./regress.conf

[ `gfsched -w | wc -l` -ge $NCOPY ] || exit $exit_unsupported

tmpf=$gftmp/foo
statf=$localtmp

if (gfhostgroup | sed 's/:/ /' > ${hostgroupfile}); then
  :
else
    exit $exit_fail
fi

hosts=`gfsched -w`
if [ $? -ne 0 -o "X${hosts}" = "X" ]; then
    exit $exit_fail
fi
nhosts=`echo ${hosts} | wc -w`
if [ $nhosts -lt $NCOPY ]; then
    exit $exit_unsupported
fi

restore_hostgroup() {
  if [ -r $hostgroupfile ]; then
    while read h g; do
      if [ "X${g}" != "X" ]; then
        gfhostgroup -s $h $g
      else
        gfhostgroup -r $h
      fi
    done < $hostgroupfile
    rm -f $hostgroupfile
  fi
}

clean_test() {
  gfrm -rf $gftmp
  rm -f $statf
}

trap 'restore_hostgroup; clean_test; exit $exit_trap' $trap_sigs

setup_test() {
  for h in $hosts; do
    if gfhostgroup -s $h test0; then
      :
    else
      restore_hostgroup
      exit $exit_fail
    fi
  done

  if gfmkdir $gftmp &&
    gfncopy -S test0:1 $gftmp && # avoid looking parent gfarm.replicainfo
    gfreg $data/1byte $tmpf; then
    :
  else
    restore_hostgroup
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
      restore_hostgroup
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
      restore_hostgroup
      clean_test
      exit $exit_fail
    fi
    sleep 1
  done
}

clean_test
setup_test

gfncopy -S test0:$NCOPY $gftmp

#wait_replication $NCOPY
gfncopy -w $tmpf
if [ $? -eq 0 ]; then
  exit_code=$exit_pass
else
  exit_code=$exit_fail
fi

restore_hostgroup
clean_test

exit $exit_code
