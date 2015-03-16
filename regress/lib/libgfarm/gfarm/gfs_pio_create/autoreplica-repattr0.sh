#!/bin/sh

basedir=`dirname $0`
. ${basedir}/autoreplica-repattr-common.sh

setup_repattr0() {
    ret=127

    n=0
    g0=0
    g1=0
    g2=0
    for __i in ${hosts}; do
	gidx=`expr ${n} % 3`
	gfhostgroup -s ${__i} "g${gidx}"
	ret=$?
	if [ ${ret} -ne 0 ]; then
	    return ${ret}
	fi
	n=`expr ${n} + 1`
	if [ $gidx -eq 0 ]; then
	    g0=`expr ${g0} + 1`
	elif [ $gidx -eq 1 ]; then
	    g1=`expr ${g1} + 1`
	elif [ $gidx -eq 2 ]; then
	    g2=`expr ${g2} + 1`
	fi
    done

    repattr="g0:${g0}, g1:${g1}, g2:${g2}"
    echo repattr=$repattr

    unset n g0 g1 g2 gidx __i
    return 0
}

#####################################################################

cleanup
setup
if [ $? -ne 0 ]; then
    onexit
    echo setup failed
    exit $exit_fail
fi
setup_repattr0
if [ $? -ne 0 ]; then
    onexit
    echo setup2 failed
    exit $exit_fail
fi

# set attr directly to the file.
gfncopy -S "${repattr}" ${file}
if [ $? -ne 0 ]; then
    onexit
    echo gfncopy -S failed
    exit $exit_fail
fi

# check where the file is.
srchost=`gfwhere ${file}`
if [ $? -ne 0 -o "X${srchost}" = "X" ]; then
    onexit
    echo gfwhere ${file} failed
    exit $exit_fail
fi

# then update the file.
write_file ${file} "${mod}"
if [ $? -ne 0 ]; then
    onexit
    echo write_file ${file} ${mod} failed
    exit $exit_fail
fi

# wait for replication.
gfncopy -w ${file} -t $GFNCOPY_TIMEOUT
if [ $? -ne 0 ]; then
    onexit
    echo gfncopy -w ${file} failed
    exit $exit_fail
fi

# check the contents of the file.
gfexport ${file} > ${tmpfile}
head=`head -c 12 ${tmpfile}`
if [ "X${mod}" != "X${head}" ]; then
    onexit
    echo diffrent contents
    exit $exit_fail
fi

onexit

exit $exit_pass
