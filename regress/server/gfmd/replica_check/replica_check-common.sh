NCOPY1=3
NCOPY2=2
NCOPY_TIMEOUT=60  # sec.
hostgroupfile=/tmp/.hostgroup.$$

check_supported_env() {
  hosts=`gfsched -w`
  if [ $? -ne 0 -o "X${hosts}" = "X" ]; then
    exit $exit_fail
  fi
  nhosts=`echo ${hosts} | wc -w`
  if [ $nhosts -lt $NCOPY1 ]; then
    echo 2>&1 "nhosts = $nhosts < $NCOPY1"
    exit $exit_unsupported
  fi
}

backup_hostgroup() {
  if (gfhostgroup | sed 's/:/ /' > ${hostgroupfile}); then
    :
  else
    exit $exit_fail
  fi
}

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
}

setup_test_ncopy() {
  if gfmkdir $gftmp &&
    set_ncopy 1 $gftmp &&  ### avoid looking parent gfarm.ncopy
    gfreg $data/1byte $tmpf; then
    :
  else
    exit $exit_fail
  fi
}

setup_test_repattr() {
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

wait_for_rep() {
  WAIT_TIME=0
  num=$1
  file=$2
  expect_timeout=$3
  while
    if [ `gfncopy -c $file` -eq $num ]; then
      exit_code=$exit_pass
      false # exit from this loop
    else
      true
    fi
  do
    WAIT_TIME=`expr $WAIT_TIME + 1`
    if [ $WAIT_TIME -gt $NCOPY_TIMEOUT ]; then
      echo replication timeout
      if [ $expect_timeout = 'true' ]; then
        return
      fi
      clean_test
      exit $exit_fail
    fi
    sleep 1
  done
}

set_ncopy() {
  if gfncopy -s $1 $2; then
    :
   else
    echo gfncopy -s failed
    clean_test
    exit $exit_fail
  fi
}

set_repattr() {
  gfncopy -S test0:$1 $2
  if [ $? -ne 0 ]; then
    echo gfncopy -S failed
    clean_test
    exit $exit_fail
  fi
}

hardlink() {
  gfln $1 $2
  if [ $? -ne 0 ]; then
    clean_test
    exit $exit_fail
  fi
}
