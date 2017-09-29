NCOPY1=3
NCOPY2=2
NCOPY_TIMEOUT=20  # sec.
hostgroupfile=/tmp/.hostgroup.$$

GRACE_SPACE_RATIO=
GRACE_TIME=

setup_test() {
  . ./regress.conf
  tmpf=$gftmp/foo
  check_supported_env
  trap 'clean_test; exit $exit_trap' $trap_sigs
  clean_test
  gfmkdir $gftmp || exit $exit_fail
  backup_hostgroup
  backup_grace
  setup_test_ncopy
  setup_test_repattr
}

clean_test() {
  restore_hostgroup
  restore_grace
  gfrm -rf $gftmp
}

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

backup_grace() {
  GRACE_SPACE_RATIO=`gfstatus -M "replica_check_remove_grace_used_space_ratio"`
  GRACE_TIME=`gfstatus -M "replica_check_remove_grace_time"`
}

restore_grace() {
  if [ -n "$GRACE_SPACE_RATIO" ]; then
    gfstatus -Mm \
      "replica_check_remove_grace_used_space_ratio $GRACE_SPACE_RATIO"
    GRACE_SPACE_RATIO=
  fi
  if [ -n "$GRACE_TIME" ]; then
    gfstatus -Mm "replica_check_remove_grace_time $GRACE_TIME"
    GRACE_TIME=
  fi
}

setup_test_ncopy() {
  if set_ncopy 1 $gftmp &&  ### avoid looking parent gfarm.ncopy
    gfreg $data/1byte $tmpf; then
    :
  else
    clean_test
    exit $exit_fail
  fi
}

setup_test_repattr() {
  for h in $hosts; do
    if gfhostgroup -s $h test0; then
      :
    else
      clean_test
      exit $exit_fail
    fi
  done

  if gfncopy -S test0:1 $gftmp && # avoid looking parent gfarm.replicainfo
    gfreg $data/1byte $tmpf; then
    :
  else
    clean_test
    exit $exit_fail
  fi
}

wait_for_rep() {
  num=$1
  file=$2
  expect_timeout=$3
  diag=$4
  WAIT_TIME=0

  if gfrepcheck stop; then
    :
  else
    clean_test
    exit $exit_fail
  fi
  if gfrepcheck start; then
    :
  else
    clean_test
    exit $exit_fail
  fi
  while
    if [ `gfncopy -c $file` -eq $num ]; then
      if [ $expect_timeout = 'true' ]; then
        echo -n "replicas: "
        gfwhere $file
        echo "unexpected: Timeout must occur."
        clean_test
        exit $exit_fail
      fi
      exit_code=$exit_pass
      false # exit from this loop
    else
      true
    fi
  do
    WAIT_TIME=`expr $WAIT_TIME + 1`
    if [ $WAIT_TIME -gt $NCOPY_TIMEOUT ]; then
      echo "replication timeout: ${diag}"
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

gfprep_n() {
  NCOPY=$1
  FILE=$2
  if gfprep $GFPREP_OPT -N $NCOPY gfarm:${FILE}; then
    :
  else
    echo failed: gfprep $GFPREP_OPT -N $NCOPY gfarm:${FILE}
    clean_test
    exit $exit_fail
  fi
}

set_grace_used_space_ratio() {
  RATIO=$1
  if gfstatus -Mm "replica_check_remove_grace_used_space_ratio ${RATIO}"; then
    :
  else
    echo failed: "replica_check_remove_grace_used_space_ratio ${RATIO}"
    clean_test
    exit $exit_fail
  fi
}

set_grace_time() {
  SEC=$1
  if gfstatus -Mm "replica_check_remove_grace_time ${SEC}"; then
    :
  else
    echo failed: "replica_check_remove_grace_time ${SEC}"
    clean_test
    exit $exit_fail
  fi
}

replica_check_remove_switch()
{
  FLAG=$1
  if gfrepcheck remove $FLAG; then
    :
  else
    echo failed: "gfrepcheck remove $FLAG"
    clean_test
    exit $exit_fail
  fi
}
