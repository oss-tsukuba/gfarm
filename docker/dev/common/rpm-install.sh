#!/bin/sh

set -eux

: $GFDOCKER_PRIMARY_USER

rpm_dir=/home/${GFDOCKER_PRIMARY_USER}/rpmbuild/RPMS
optional=false

usage()
{
  echo >&2 \
	"Usage: $0 [-O] <package-name>"
  exit 2
}

while getopts Od:s:v: option
do
  case $option in
  O) optional=true;;
  '?') usage;;
  esac
done
shift $((OPTIND - 1))

case $# in
1) name=$1;;
*) usage;;
esac


if $optional && ! ls ${rpm_dir}/*/${name}-*.rpm >/dev/null
then
  echo >&2 "WARNING: ${name}: RPM files not found, skip installation stage"
  exit 0	# this is OK at least for now
fi

rpm -ivh ${rpm_dir}/*/${name}-*.rpm
