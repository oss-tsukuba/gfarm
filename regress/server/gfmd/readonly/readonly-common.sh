. ./regress.conf

HOST_INFO_FLAG_READONLY=1
N_REQUIRED_SDHOSTS=2
HOST_FLAGS_MAP_FILE="${localtmp}/readonly-common-host-flags.txt"

GFPREP=$regress/bin/gfprep_for_test

GFS_PIO_TEST_P=${base}/../../../lib/libgfarm/gfarm/gfs_pio_test/gfs_pio_test
update_file() {
  #V="-v"
  V=""
  #echo "echo 12345 | $GFS_PIO_TEST_P $V -w -W 5 $@"
  echo 12345 | $GFS_PIO_TEST_P $V -w -W 5 $@
  return $?
}

del_testdir() {
  rm -rf "$localtmp"
  gfrm -rf "$gftmp"
}

clean_test() {
  restore_host_flags
  del_testdir
}

check_supported_env() {
  diag=check_supported_env
  if $regress/bin/am_I_gfarmadm; then
    :
  else
    exit $exit_unsupported
  fi

  sdhosts="$(gfsched -w)"  # use writable hosts only
  if [ "$?" -ne 0 ] || [ "X${sdhosts}" = X ]; then
    echo "failed: ${diag}"
    exit $exit_fail
  fi
  nsdhosts="$(echo "$sdhosts" | wc -l)"
  if [ "$nsdhosts" -lt "$N_REQUIRED_SDHOSTS" ]; then
    echo "${diag}: nsdhosts = ${nsdhosts} < ${N_REQUIRED_SDHOSTS}"
    exit $exit_unsupported
  fi
}

backup_host_flags() {
  gfhost -M | cut -d' ' -f3,5 > "$HOST_FLAGS_MAP_FILE"
}

restore_host_flags() {
  diag=restore_host_flags
  while IFS= read -r entry; do
    host="$(echo "$entry" | cut -d' ' -f1)"
    flag="$(echo "$entry" | cut -d' ' -f2)"
    gfhost -m -f "$flag" "$host"
    if [ "$?" -ne 0 ]; then
      echo "failed: ${diag}"
      exit_code="$exit_trap"
      exit
    fi
  done < "$HOST_FLAGS_MAP_FILE"
  rm -rf "$HOST_FLAGS_MAP_FILE"
}

query_host_flags() {
  host="$1"
  gfhost -M "$host" | cut -d' ' -f5
}

query_host_flags_by_l() {
  host="$1"
  gfhost -l "$host" | cut -d' ' -f7 | sed -E 's/^([0-9]+).*$/\1/'
}

set_readonly_flag() {
  flags="$1"
  echo "$((flags | HOST_INFO_FLAG_READONLY))"
}

unset_readonly_flag() {
  flags="$1"
  echo "$((flags & ~HOST_INFO_FLAG_READONLY))"
}

test_readonly_flag_common() {
  host="$1"
  flags="$2"
  cmd_gfmd_restart_all="$3"
  diag="$4"
  gfhost -m -f "$flags" "$host"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfhost"
    exit
  fi
  if ! "$cmd_gfmd_restart_all"; then
    echo "failed: ${diag}: restart gfmd"
    exit_code="$exit_trap"
    exit
  fi
  if [ "$flags" -ne "$(query_host_flags "$host")" ]; then
    echo "failed: ${diag}: query_host_flags"
    exit
  fi
  if [ "$flags" -ne "$(query_host_flags_by_l "$host")" ]; then
    echo "failed: ${diag}: query_host_flags"
    exit
  fi
}

test_set_readonly_flag() {
  diag=test_set_readonly_flag
  test_readonly_flag_common "$1" "$(set_readonly_flag "$2")" "$3" "$diag"
}

test_unset_readonly_flag() {
  diag=test_set_readonly_flag
  test_readonly_flag_common "$1" "$(unset_readonly_flag "$2")" "$3" "$diag"
}

# expect metadb_server_heartbeat_interval in gfmd.conf
metadb_server_heartbeat_interval=180

rep_retry() {
   cmd=$1
   shift
   sleep_time=2
   timeout=`expr $metadb_server_heartbeat_interval + 2`

   sec=0
   while :; do
      $cmd -q $opts $@ && return 0
      [ $sec -ge $timeout ] && break
      sec=`expr $sec + ${sleep_time}`
      #echo $cmd -q $opts $@
      echo "[${sec}] rep_retry(${cmd}): wait for removing /tmp/gfsd-readonly-*"
      #gfdf -ih
      #gfdf -h
      sleep ${sleep_time}
   done
   echo "${cmd}_retry: timeout (${timeout})"
   return 1
}

gfrep_retry() {
   rep_retry gfrep $@
}

gfprep_retry() {
   rep_retry "$GFPREP" $@
}

test_gfrep_1_to_2() {
  cmd_gfrep="$1"
  test_file="$2"
  diag="${3}: test_gfrep_1_to_2"
  "$cmd_gfrep"_retry -N 2 "$test_file"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: ${cmd_gfrep}"
    exit
  fi
}

test_gfrep_2_to_1() {
  cmd_gfrep="$1"
  test_file="$2"
  diag="${3}: test_gfrep_2_to_1"
  "$cmd_gfrep"_retry -x -N 1 "$test_file"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: ${cmd_gfrep}"
    exit
  fi
}

test_gfrep_common() {
  cmd_gfrep="$1"
  ro_before_rep="$2"
  diag="${3}: test_gfrep_common"
  gf_test_file="gfarm://${gftmp}/test"

  rohosts="$(gfsched -w | tail -n +3)"  # except 2 host
  save_IFS="$IFS"
  IFS='
'
  for host in $rohosts; do
    flags="$(query_host_flags "$host")"
    gfhost -m -f "$(set_readonly_flag "$flags")" "$host"
    if [ "$?" -ne 0 ]; then
      echo "failed: ${diag}: gfhost"
      exit
    fi
  done
  IFS="$save_IFS"

  nhosts="$(gfsched -w | wc -l)"
  if [ "$nhosts" -ne 2 ]; then
    echo "unexpected condition: require writable hosts at least 2"
    exit
  fi
  rohost="$(gfsched -w | head -n 1)"
  flags="$(query_host_flags "$rohost")"
  gfreg -h "$rohost" "${data}/1byte" "$gf_test_file"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfreg"
    exit
  fi

  if "$ro_before_rep"; then
    gfhost -m -f "$(set_readonly_flag "$flags")" "$rohost"
    if [ "$?" -ne 0 ]; then
      echo "failed: ${diag}: gfhost"
      exit
    fi
  fi
  test_gfrep_1_to_2 "$cmd_gfrep" "$gf_test_file" "$diag"
  if ! "$ro_before_rep"; then
    gfhost -m -f "$(set_readonly_flag "$flags")" "$rohost"
    if [ "$?" -ne 0 ]; then
      echo "failed: ${diag}: gfhost"
      exit
    fi
  fi
  test_gfrep_2_to_1 "$cmd_gfrep" "$gf_test_file" "$diag"
}

test_gfrep() {
  diag=test_gfrep
  test_gfrep_common gfrep false "$diag"
}

test_gfprep() {
  diag=test_gfprep
  test_gfrep_common gfprep false "$diag"
}

test_gfrep_ro_before_rep() {
  diag=test_gfrep
  test_gfrep_common gfrep true "$diag"
}

test_gfprep_ro_before_rep() {
  diag=test_gfprep
  test_gfrep_common gfprep true "$diag"
}


# setup test

check_supported_env
trap 'clean_test; exit "$exit_trap"' $trap_sigs
trap 'clean_test; exit "$exit_code"' 0
del_testdir
mkdir "$localtmp" || exit
gfmkdir -p "$gftmp" || exit
backup_host_flags
