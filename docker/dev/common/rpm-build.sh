#!/bin/sh

set -eux

optional=false
files=
tools=
srcdir=
buildenv=
specpath=
ver=

usage()
{
  echo >&2 \
	Usage: $0 '[-O] [-F "<required_files>..."] [-T "<required_tools>..."]'
	'[-d <srcdir>] [-s <specfile-path>] [-v <version>] <package-name>'
  exit 2
}

while getopts OF:T:d:e:s:v: option
do
  case $option in
  O) optional=true;;
  F) files=$OPTARG;;
  T) tools=$OPTARG;;
  d) srcdir=$OPTARG;;
  e) buildenv=$OPTARG;;
  s) specpath=$OPTARG;;
  v) ver=$OPTARG;;
  '?') usage;;
  esac
done
shift $((OPTIND - 1))

case $# in
1) name=$1;;
*) usage;;
esac

: ${srcdir:=/work/gfarm/${name}}

# some package is optional, skip builing if it is optional
if $optional && [ ! -d "${srcdir}" ]; then
  echo >&2 "WARNING: ${name}: repository not found, skip RPM build stage"
  exit 0
fi

skip_building()
{
  if $optional; then
    echo >&2 "WARNING: ${name}: $*, skip RPM build stage"
    exit 0
  else
    echo >&2 "ERROR: ${name}: $*, error on RPM build stage"
    exit 1
  fi
}

# do required files exist? otherwise skip buidling
case ${files:+set} in
set)
  for file in ${files}
  do
    [ -f "${file}" ] || skip_building "file ${file} not found"
  done;;
esac

# do required tools exist? otherwise skip buidling
case ${tools:+set} in
set)
  for tool in ${tools}
  do
    type ${tool} >/dev/null 2>&1 || skip_building "tool ${tool} not found"
  done;;
esac

case ${specpath:+set} in
set)
  case ${specpath} in
  /*) spec=${specpath};;
  *)  spec=${srcdir}/${specpath};;
  esac;;
*)
  spec=${srcdir}/${name}.spec;;
esac

if [ ! -f "${spec}" ]; then
  echo >&2 "ERROR: ${name}: ${spec} does not exit, error on RPM build stage"
  exit 1
fi

case ${ver:+set} in
set) :;;
*)
  ver=$(grep '^Version:' ${spec} | awk '{print $2}');;
esac

mkdir -p "${HOME}"/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPEC,SPECS,SRPMS}

name_ver=${name}-${ver}
targz=${name_ver}.tar.gz
srpm="${HOME}/rpmbuild/SRPMS/${name_ver}-*.src.rpm"

cp -a ${srcdir} "${HOME}/rpmbuild/SOURCES/${name_ver}"

(cd "${HOME}/rpmbuild/SOURCES" &&
  tar --exclude=.svn --exclude=.git --owner=root --group=root -zcvf \
    ${targz} ${name_ver})

rpmbuild -bs ${spec}
env ${buildenv:+"${buildenv}"} rpmbuild --rebuild ${srpm}
