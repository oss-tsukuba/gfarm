#!/bin/sh

# XXX FIXME: runnig this script simultaneously is not safe

. ./regress.conf

#####################################################################

repattr="g0:10, g1:10, g2:10"
dir=/tmp/autoreplica-test.$$
file=${dir}/file.$$
gfs_pio_test=`dirname $testbin`/gfs_pio_test/gfs_pio_test
tmpfile=/tmp/.autoreplica-test.junk.$$
hostgroupfile=/tmp/.hostgroup.$$

wait=10

mod="over written"

#####################################################################

hosts=`gfsched -w`
if test $? -ne 0 -o "X${hosts}" = "X"; then
    exit $exit_fail
fi
nhosts=`echo ${hosts} | wc -w`
if test ${nhosts} -lt 3; then
    exit $exit_unsupported
fi
gfhostgroup | sed 's/:/ /' > ${hostgroupfile}
if test $? -ne 0; then
    exit $exit_fail
fi

#####################################################################

cleanup() {
    rm -f ${tmpfile}
    gfrepattr -r ${file} > /dev/null 2>&1
    gfrm -f ${file} > /dev/null 2>&1
    gfrepattr -r ${dir} > /dev/null 2>&1
    gfrmdir ${dir} > /dev/null 2>&1
    for __i in ${hosts}; do
	gfhostgroup -r ${__i} > /dev/null 2>&1
    done
    unset __i
}

restore_hostgroup() {
    if test -r ${hostgroupfile}; then
	while read h g; do
	    if test "X${g}" != "X"; then
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
    ret=127

    g=0
    for __i in ${hosts}; do
	gidx=`expr ${g} % 3`
	gfhostgroup -s ${__i} "g${gidx}"
	ret=$?
	g=`expr ${g} + 1`
    done
    if test ${ret} -ne 0; then
	return ${ret}
    fi

    unset g gidx __i

    gfmkdir ${dir}
    ret=$?
    if test ${ret} -ne 0; then
	return ${ret}
    fi

    gfreg /etc/group ${file}
    ret=$?
    if test ${ret} -ne 0; then
	return ${ret}
    fi

    return ${ret}
}

write_file() {
    dst=$1
    shift
    val="${1+$@}"
    len=`printf '%s' "${val}" | wc -c`
#   pass "-t" option to ${gfs_pio_test} to avoid the following bug:
#	https://sourceforge.net/apps/trac/gfarm/ticket/461
#   echo "${val}" | ${gfs_pio_test} -w -W${len} ${dst}
    echo "${val}" | ${gfs_pio_test} -wt -W${len} ${dst}
    ret=$?
    unset val len dst
    return ${ret}
}

trap 'onexit; exit $exit_trap' $trap_sigs

#####################################################################

cleanup
setup
if test $? -ne 0; then
    onexit
    exit $exit_fail
fi

# set attr directly to the file.
gfrepattr -s ${file} "${repattr}"
if test $? -ne 0; then
    onexit
    exit $exit_fail
fi

# check where the file is.
srchost=`gfwhere ${file}`
if test $? -ne 0 -o "X${srchost}" = "X"; then
    onexit
    exit $exit_fail
fi

# then update the file.
write_file ${file} "${mod}"
if test $? -ne 0; then
    onexit
    exit $exit_fail
fi

sleep ${wait}

# check replication status.
allhosts=`gfwhere ${file}`
if test $? -ne 0; then
    onexit
    exit $exit_fail
fi
if test "X${srchost}" = "X${allhosts}"; then
    onexit
    exit $exit_fail
fi

# check the contents of the file.
gfexport ${file} > ${tmpfile}
head=`head -c 12 ${tmpfile}`
if test "X${mod}" != "X${head}"; then
    onexit
    exit $exit_fail
fi

onexit

exit $exit_pass
