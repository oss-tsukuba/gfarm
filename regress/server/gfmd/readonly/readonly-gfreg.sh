#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

hosts="$(gfsched -w)"  # use writable hosts only
rohost="$(echo "$hosts" | head -n 1)"
flags="$(query_host_flags "$rohost")"
if [ "$?" -ne 0 ] || [ "X${flags}" = X ]; then
    echo "failed: query_host_flags @ $0"
    exit
fi

SHORT_TIMEOUT_CONF_FILE="${localtmp}/SHORT_timeout.gfarm2.conf"
cat << __EOF__ >> "$SHORT_TIMEOUT_CONF_FILE" || exit
no_file_system_node_timeout 3
#log_level debug
__EOF__


test_readonly_enable() {
  host="$1"
  flags="$2"
  diag=test_readonly_enable

  gfhost -m -f "$(set_readonly_flag "$flags")" "$host"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfhost"
    exit
  fi
  gfreg -h "$host" "${data}/1byte" "${gftmp}/test1"
  if [ "$?" -eq 0 ]; then
    echo "unexpected success: ${diag}: gfreg -h ${host}"
    exit
  fi
  # no space
}

test_readonly_disable() {
  host="$1"
  flags="$2"
  diag=test_readonly_disable

  gfhost -m -f "$(unset_readonly_flag "$flags")" "$host"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfhost"
    exit
  fi
  gfreg -h "$host" "${data}/1byte" "${gftmp}/test2"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfreg -h ${host}"
    exit
  fi
}

test_readonly_enable_all_host() {
  diag=test_readonly_enable_all_host

  _IFS="$IFS"
  IFS='
'
  for host in $hosts; do
    flags="$(query_host_flags "$host")"
    if [ "$?" -ne 0 ] || [ "X${flags}" = X ]; then
        echo "failed: query_host_flags @ ${diag}"
        exit
    fi
    gfhost -m -f "$(set_readonly_flag "$flags")" "$host"
    if [ "$?" -ne 0 ]; then
        echo "failed: ${diag}: gfhost"
        exit
    fi
  done
  IFS="$_IFS"

  export GFARM_CONFIG_FILE="$SHORT_TIMEOUT_CONF_FILE"
  gfreg "${data}/1byte" "${gftmp}/test3"
  if [ "$?" -eq 0 ]; then
    echo "unexpected success: ${diag}: gfreg"
    exit
  fi
  unset GFARM_CONFIG_FILE
}

test_one_writable_host() {
  diag=test_one_writable_host

  _IFS="$IFS"
  IFS='
'
  for host in $hosts; do
    flags="$(query_host_flags "$host")"
    if [ "$?" -ne 0 ] || [ "X${flags}" = X ]; then
        echo "failed: ${diag}: query_host_flags"
        exit
    fi
    gfhost -m -f "$(set_readonly_flag "$flags")" "$host"
    if [ "$?" -ne 0 ]; then
        echo "failed: ${diag}: gfhost"
        exit
    fi
  done
  IFS="$_IFS"

  whost="$(echo "$hosts" | head -n 1)"
  wflags="$(query_host_flags "$whost")"
  if [ "$?" -ne 0 ] || [ "X${wflags}" = X ]; then
      echo "failed: query_host_flags @ ${diag}"
      exit
  fi
  gfhost -m -f "$(unset_readonly_flag "$wflags")" "$whost"
  if [ "$?" -ne 0 ]; then
      echo "failed: ${diag}: gfhost"
      exit
  fi

  gfreg "${data}/1byte" "${gftmp}/test4"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfreg"
    exit
  fi
}

test_readonly_enable "$rohost" "$flags"
test_readonly_disable "$rohost" "$flags"
test_readonly_enable_all_host # retrying due to "no filesystem node"
test_one_writable_host

exit_code="$exit_pass"
