#! /bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

ROHOST="$(gfsched -w | head -n 1)"
FLAGS="$(query_host_flags "$ROHOST")"
GF_TEST_FILE="${gftmp}/test"
REP_ENABLE_CONF_FILE="${localtmp}/enable.gfarm2.conf"
REP_DISABLE_CONF_FILE="${localtmp}/disable.gfarm2.conf"
if [ X"$GFARM_CONFIG_FILE" != X ]; then
    conf_file="$GFARM_CONFIG_FILE"
else
    conf_file=~/.gfarm2rc
fi
if [ -r "$conf_file" ]; then
    cp "$conf_file" "$REP_ENABLE_CONF_FILE" || exit
    cp "$conf_file" "$REP_DISABLE_CONF_FILE" || exit
fi
cat << __EOF__ >> "$REP_ENABLE_CONF_FILE" || exit
replication_at_write_open enable
#log_level debug
__EOF__
cat << __EOF__ >> "$REP_DISABLE_CONF_FILE" || exit
replication_at_write_open disable
#log_level debug
__EOF__

prepare_file() {
  CONFIG_FILE="$1"
  shift
  srcfile="$1"
  GFARM_CONFIG_FILE="$CONFIG_FILE" gfrm -f "$GF_TEST_FILE"
  #gfhost -M "$ROHOST"
  #gfsched -w
  #gfdf -h
  #gfdf -ih
  #echo gfreg -h "$ROHOST" "$srcfile" "$GF_TEST_FILE"
  GFARM_CONFIG_FILE="$CONFIG_FILE" gfreg -h "$ROHOST" "$srcfile" "$GF_TEST_FILE" || exit
  GFARM_CONFIG_FILE="$CONFIG_FILE" gfhost -m -f "$(set_readonly_flag "$FLAGS")" "$ROHOST" || exit
}

cleanup_file() {
  CONFIG_FILE="$1"
  GFARM_CONFIG_FILE="$CONFIG_FILE" gfhost -m -f "$(unset_readonly_flag "$FLAGS")" "$ROHOST" || exit
  GFARM_CONFIG_FILE="$CONFIG_FILE" gfrm -f "$GF_TEST_FILE"
}

test_rawo_disable_1B_common() {
  opts="$1"
  diag="$2"
  prepare_file "$REP_DISABLE_CONF_FILE" "${data}/1byte"
  update_file "$REP_DISABLE_CONF_FILE" $opts "$GF_TEST_FILE"
  if [ "$?" -eq 0 ]; then
    echo "unexpected success: ${diag}"
    exit
  fi
  cleanup_file "$REP_DISABLE_CONF_FILE"
}

test_rawo_disable_1B_writeopen() {
  diag=test_rawo_disable_1B_writeopen
  test_rawo_disable_1B_common "" "$diag"
}

test_rawo_disable_1B_create() {
  diag=test_rawo_disable_1B_create
  test_rawo_disable_1B_common "-c" "$diag"
}

test_rawo_disable_0B_common() {
  opts="$1"
  diag="$2"
  prepare_file "$REP_DISABLE_CONF_FILE" "${data}/0byte"
  update_file "$REP_DISABLE_CONF_FILE" $opts "$GF_TEST_FILE"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}"
    exit
  fi
  cleanup_file "$REP_DISABLE_CONF_FILE"
}

test_rawo_disable_0B_writeopen() {
  diag=test_rawo_disable_0B_writeopen
  test_rawo_disable_0B_common "" "$diag"
}

test_rawo_disable_0B_create() {
  diag=test_rawo_disable_0B_create
  test_rawo_disable_0B_common "-c" "$diag"
}

test_rawo_disable_1B_truncate_common() {
  opts="$1"
  diag="$2"
  prepare_file "$REP_DISABLE_CONF_FILE" "${data}/1byte"
  update_file "$REP_DISABLE_CONF_FILE" -t $opts "$GF_TEST_FILE"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}"
    exit
  fi
  cleanup_file "$REP_DISABLE_CONF_FILE"
}

test_rawo_disable_1B_truncate_writeopen() {
  diag=test_rawo_disable_1B_truncate_writeopen
  test_rawo_disable_1B_truncate_common "" "$diag"
}

test_rawo_disable_1B_truncate_create() {
  diag=test_rawo_disable_1B_truncate_create
  test_rawo_disable_1B_truncate_common "-c" "$diag"
}

test_rawo_enable_1B_specified_host_common() {
  opts="$1"
  diag="$2"
  prepare_file "$REP_ENABLE_CONF_FILE" "${data}/1byte"
  update_file "$REP_ENABLE_CONF_FILE" -h "$ROHOST" $opts "$GF_TEST_FILE"
  if [ "$?" -eq 0 ]; then
    echo "unexpected success: ${diag}"
    exit
  fi
  cleanup_file "$REP_ENABLE_CONF_FILE"
}

test_rawo_enable_1B_specified_host_writeopen() {
  diag=test_rawo_enable_1B_specified_host_writeopen
  test_rawo_enable_1B_specified_host_common "" "$diag"
}

test_rawo_enable_1B_specified_host_create() {
  diag=test_rawo_enable_1B_specified_host_create
  test_rawo_enable_1B_specified_host_common "-c" "$diag"
}

test_rawo_enable_1B_common() {
  opts="$1"
  diag="$2"
  prepare_file "$REP_ENABLE_CONF_FILE" "${data}/1byte"
  update_file "$REP_ENABLE_CONF_FILE" $opts "$GF_TEST_FILE"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}"
    exit
  fi
  cleanup_file "$REP_ENABLE_CONF_FILE"
}

test_rawo_enable_1B_writeopen() {
  diag=test_rawo_enable_1B_writeopen
  test_rawo_enable_1B_common "" "$diag"
}

test_rawo_enable_1B_create() {
  diag=test_rawo_enable_1B_create
  test_rawo_enable_1B_common "-c" "$diag"
}

simultaneous_common() {
  remove=$1

  diag=test_rep_enable_writeopen_simultaneous
  if [ $remove = 'true' ]; then
    diag=${diag}_and_remove
  fi
  srcfile="${localtmp}/bigfile"
  dd if=/dev/zero "of=${srcfile}" bs=1M count=500 2> /dev/null
  prepare_file "$REP_ENABLE_CONF_FILE" "$srcfile"
  update_file "$REP_ENABLE_CONF_FILE" "$GF_TEST_FILE" &
  p1=$!
  update_file "$REP_ENABLE_CONF_FILE" "$GF_TEST_FILE" &
  p2=$!
  sleep 1
  if [ $remove = 'true' ]; then
    GFARM_CONFIG_FILE="$REP_ENABLE_CONF_FILE" gfrm "$GF_TEST_FILE" || exit
  fi
  wait $p1
  r1=$?
  wait $p2
  r2=$?
  cleanup_file "$REP_ENABLE_CONF_FILE"
  if [ $remove = 'true' ]; then
    if [ "$r1" -eq 0 -a "$r2" -eq 0 ]; then
      echo "XFAIL: error is not occurred: ${diag}"
      echo "Threre is a possibility that two update_file exit before gfrm."
      exit_code="$exit_xfail"
      exit
    fi
    # one update_file may succeed
  else
    if [ "$r1" -ne 0 -o "$r2" -ne 0 ]; then
      echo "failed: ${diag}"
      exit
    fi
  fi
}

test_rawo_enable_writeopen_simultaneous() {
    simultaneous_common false
}

test_rawo_enable_writeopen_simultaneous_and_remove() {
    simultaneous_common true
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
test_rawo_enable_writeopen_simultaneous_and_remove  # may be XFAIL

exit_code="$exit_pass"
