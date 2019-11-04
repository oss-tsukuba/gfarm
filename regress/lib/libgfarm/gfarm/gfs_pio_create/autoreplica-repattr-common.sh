#!/bin/sh

# XXX FIXME: runnig this script simultaneously is not safe

. ./regress.conf
GFNCOPY_TIMEOUT=60

#####################################################################

dir=/tmp/autoreplica-test.$$
file=${dir}/file.$$
gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
tmpfile=/tmp/.autoreplica-test.junk.$$
hostgroupfile=/tmp/.hostgroup.$$

mod="over written"

#####################################################################

if $regress/bin/am_I_gfarmadm; then
  :
else
  exit $exit_unsupported
fi

hosts=`gfsched -w`
if [ $? -ne 0 -o "X${hosts}" = "X" ]; then
    exit $exit_fail
fi
nhosts=`echo ${hosts} | wc -w`
if [ ${nhosts} -lt 3 ]; then
    echo gfsd nodes at least three is necessary
    exit $exit_unsupported
fi
gfhostgroup | sed 's/:/ /' > ${hostgroupfile}
if [ $? -ne 0 ]; then
    exit $exit_fail
fi

#####################################################################

cleanup() {
    rm -f ${tmpfile}
    gfncopy -r ${file} > /dev/null 2>&1
    gfrm -f ${file} > /dev/null 2>&1
    gfncopy -r ${dir} > /dev/null 2>&1
    gfrmdir ${dir} > /dev/null 2>&1
    for __i in ${hosts}; do
	gfhostgroup -r ${__i} > /dev/null 2>&1
    done
    unset __i
}

restore_hostgroup() {
    if [ -r ${hostgroupfile} ]; then
	while read h g; do
	    if [ "X${g}" != "X" ]; then
		gfhostgroup -s ${h} ${g}
	    else
		gfhostgroup -r ${h}
	    fi
	done < ${hostgroupfile}
	rm -f ${hostgroupfile}
    fi
}

onexit() {
    cleanup
    restore_hostgroup
}

setup() {
    gfmkdir ${dir}
    ret=$?
    if [ ${ret} -ne 0 ]; then
	return ${ret}
    fi
    # disable automatic replication
    gfncopy -s 1 ${dir}
    ret=$?
    if [ ${ret} -ne 0 ]; then
	return ${ret}
    fi

    gfreg /etc/group ${file}
    ret=$?
    if [ ${ret} -ne 0 ]; then
	return ${ret}
    fi

    # the mode of /etc/group may be 444
    gfchmod 644 ${file}
    ret=$?
    if [ ${ret} -ne 0 ]; then
	return ${ret}
    fi

    return ${ret}
}

write_file() {
    dst=$1
    shift
    val="${1+$@}"
    len=`echo -n ${val} | wc -c`
#   pass "-t" option to ${gfs_pio_test} to avoid the following bug:
#	https://sourceforge.net/apps/trac/gfarm/ticket/461
#   echo "${val}" | ${gfs_pio_test} -w -W${len} ${dst}
    echo "${val}" | ${gfs_pio_test} -wt -W${len} ${dst}
    ret=$?
    unset val len dst
    return ${ret}
}

trap 'onexit; exit $exit_trap' $trap_sigs
