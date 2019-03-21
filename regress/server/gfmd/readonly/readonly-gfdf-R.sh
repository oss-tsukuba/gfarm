#!/bin/sh

base="$(dirname "$0")"
. "${base}/readonly-common.sh"

hosts="$(gfsched -w)"  # use writable hosts only
rohost="$(echo "$hosts" | head -n 1)"
flags="$(query_host_flags "$rohost")"
if [ "$?" -ne 0 ] || [ "X${flags}" = X ]; then
    echo "failed: $0"
    exit
fi

get_avail() {
  host="$1"
  opts="$2"

  out=$(gfdf $opts "$host") || exit
  echo "$out" | awk 'NR==2 {print $3}'
}

compare_avail() {
  withoutR1=$1
  withR=$2
  withoutR2=$3
  [ $withoutR1 = $withR ] && return
  echo "INFO: Avail is updated by heartbeat"
  [ $withoutR2 = $withR ] && return

  # unexpected
  echo "Avail (from gfdf)   : ${withoutR}"
  echo "Avail (from gfdf -R): ${withR}"
  exit
}

test_gfdf_R() {
  host="$1"
  diag=test_gfdf_R

  avail1=$(get_avail "$host" "")
  avail_R=$(get_avail "$host" "-R")
  avail2=$(get_avail "$host" "")
  compare_avail $avail1 $avail_R $avail2
}

isnot0() {
  avail=$1
  d=$2
  if [ $avail -eq 0 ]; then
    echo "unexpected: avail=0: ${d}"
    exit
  fi
}

is0() {
  avail=$1
  d=$2
  if [ $avail -ne 0 ]; then
    echo "unexpected: avail!=0 (${avail}): ${d}"
    exit
  fi
}

test_gfdf_R_readonly() {
  host="$1"
  diag=test_gfdf_R_readonly

  avail1=$(get_avail "$host" "")
  isnot0 $avail1 "${diag}/avail1"

  gfhost -m -f "$(set_readonly_flag "$flags")" "$host"
  if [ "$?" -ne 0 ]; then
    echo "failed: ${diag}: gfhost"
    exit
  fi

  avail2=$(get_avail "$host" "")
  is0 $avail2 "${diag}/avail2"

  avail_R=$(get_avail "$host" "-R")
  isnot0 $avail_R "${diag}/avail1"
}

test_gfdf_R "$rohost"
test_gfdf_R_readonly "$rohost"

exit_code="$exit_pass"
