#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

ROHOST="$(gfsched -w | head -n 1)"
FLAGS="$(query_host_flags "$ROHOST")"
GF_TEST_FILE="${gftmp}/test"
GFS_PIO_TEST="${base}/../../../lib/libgfarm/gfarm/gfs_pio_test/gfs_pio_test"
#GFS_PIO_TEST_V="-v"
GFS_PIO_TEST_V=""

REP_ENABLE_CONF_FILE="${localtmp}/enable.gfarm2.conf"
REP_DISABLE_CONF_FILE="${localtmp}/disable.gfarm2.conf"
cat << __EOF__ > "$REP_ENABLE_CONF_FILE" || exit
replication_at_write_open enable
#log_level debug
__EOF__
cat << __EOF__ > "$REP_DISABLE_CONF_FILE" || exit
replication_at_write_open disable
#log_level debug
__EOF__

prepare_file() {
  srcfile="$1"
  gfreg -h "$ROHOST" "$srcfile" "$GF_TEST_FILE" || exit
  gfhost -m -f "$(set_readonly_flag "$FLAGS")" "$ROHOST" || exit
}

cleanup_file() {
  gfhost -m -f "$(unset_readonly_flag "$FLAGS")" "$ROHOST" || exit
  gfrm -f "$GF_TEST_FILE"
}

test_rawo_disable_1B_common() {
  is_create="$1"
  diag="$2"
  opts=
  if "$is_create"; then
    opts=-c
  fi
  export GFARM_CONFIG_FILE="$REP_DISABLE_CONF_FILE"
  prepare_file "${data}/1byte"
  echo 12345 | "$GFS_PIO_TEST" $GFS_PIO_TEST_V -w $opts "$GF_TEST_FILE"
  if [ "$?" -eq 0 ]; then
    echo "unexpected success: ${diag}"
    exit
  fi
  cleanup_file
  unset GFARM_CONFIG_FILE
}

test_rawo_disable_1B_writeopen() {
  diag=test_rawo_disable_1B_writeopen
  test_rawo_disable_1B_common false "$diag"
}

test_rawo_disable_1B_create() {
  diag=test_rawo_disable_1B_create
  test_rawo_disable_1B_common true "$diag"
}

test_rawo_disable_0B_common() {
  is_create="$1"
  diag="$2"
  opts=
  if "$is_create"; then
    opts=-c
  fi
  export GFARM_CONFIG_FILE="$REP_DISABLE_CONF_FILE"
  prepare_file "${data}/0byte"
  echo 12345 | "$GFS_PIO_TEST" $GFS_PIO_TEST_V -w $opt "$GF_TEST_FILE"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}"
    exit
  fi
  cleanup_file
  unset GFARM_CONFIG_FILE
}

test_rawo_disable_0B_writeopen() {
  diag=test_rawo_disable_0B_writeopen
  test_rawo_disable_0B_common false "$diag"
}

test_rawo_disable_0B_create() {
  diag=test_rawo_disable_0B_create
  test_rawo_disable_0B_common true "$diag"
}

test_rawo_disable_1B_truncate_common() {
  is_create="$1"
  diag="$2"
  opts=
  if "$is_create"; then
    opts=-c
  fi
  export GFARM_CONFIG_FILE="$REP_DISABLE_CONF_FILE"
  prepare_file "${data}/1byte"
  echo 12345 | "$GFS_PIO_TEST" $GFS_PIO_TEST_V -t -w $opt "$GF_TEST_FILE"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}"
    exit
  fi
  cleanup_file
  unset GFARM_CONFIG_FILE
}

test_rawo_disable_1B_truncate_writeopen() {
  diag=test_rawo_disable_1B_truncate_writeopen
  test_rawo_disable_1B_truncate_common false "$diag"
}

test_rawo_disable_1B_truncate_create() {
  diag=test_rawo_disable_1B_truncate_create
  test_rawo_disable_1B_truncate_common true "$diag"
}

test_rawo_enable_1B_specified_host_common() {
  is_create="$1"
  diag="$2"
  opts=
  if "$is_create"; then
    opts=-c
  fi
  export GFARM_CONFIG_FILE="$REP_ENABLE_CONF_FILE"
  prepare_file "${data}/1byte"
  echo 12345 \
  | "$GFS_PIO_TEST" $GFS_PIO_TEST_V -h "$ROHOST" -w $opt "$GF_TEST_FILE"
  if [ "$?" -eq 0 ]; then
    echo "unexpected success: ${diag}"
    exit
  fi
  cleanup_file
  unset GFARM_CONFIG_FILE
}

test_rawo_enable_1B_specified_host_writeopen() {
  diag=test_rawo_enable_1B_specified_host_writeopen
  test_rawo_enable_1B_specified_host_common false "$diag"
}

test_rawo_enable_1B_specified_host_create() {
  diag=test_rawo_enable_1B_specified_host_create
  test_rawo_enable_1B_specified_host_common true "$diag"
}

test_rawo_enable_1B_common() {
  is_create="$1"
  diag="$2"
  opts=
  if "$is_create"; then
    opts=-c
  fi
  export GFARM_CONFIG_FILE="$REP_ENABLE_CONF_FILE"
  prepare_file "${data}/1byte"
  echo 12345 | "$GFS_PIO_TEST" $GFS_PIO_TEST_V -w $opt "$GF_TEST_FILE"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}"
    exit
  fi
  cleanup_file
  unset GFARM_CONFIG_FILE
}

test_rawo_enable_1B_writeopen() {
  diag=test_rawo_enable_1B_writeopen
  test_rawo_enable_1B_common false "$diag"
}

test_rawo_enable_1B_create() {
  diag=test_rawo_enable_1B_create
  test_rawo_enable_1B_common true "$diag"
}

test_rawo_enable_writeopen_simultaneous() {
  diag=test_rep_enable_writeopen_simultaneous
  export GFARM_CONFIG_FILE="$REP_ENABLE_CONF_FILE"
  srcfile="${localtmp}/2GB"
  dd if=/dev/zero "of=${srcfile}" bs=1M count=2000
  prepare_file "$srcfile"
  echo 12345 | "$GFS_PIO_TEST" $GFS_PIO_TEST_V -w "$GF_TEST_FILE" &
  echo 12345 | "$GFS_PIO_TEST" $GFS_PIO_TEST_V -w "$GF_TEST_FILE"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}"
    exit
  fi
  wait
  unset GFARM_CONFIG_FILE
}


### rawo: replication_at_write_open

test_rawo_disable_1B_writeopen
test_rawo_disable_1B_create
test_rawo_disable_0B_writeopen
test_rawo_disable_0B_create
test_rawo_disable_1B_truncate_writeopen
test_rawo_disable_1B_truncate_create
test_rawo_enable_1B_specified_host_writeopen
test_rawo_enable_1B_specified_host_create
test_rawo_enable_1B_writeopen
test_rawo_enable_1B_create
test_rawo_enable_writeopen_simultaneous

exit_code="$exit_pass"
