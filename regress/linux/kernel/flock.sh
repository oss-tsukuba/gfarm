#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

testfile=${MOUNTPOINT}/file
testbin=${TESTBINDIR}/flock

waitsec=2
sharesec=${waitsec}
exsec=`expr ${waitsec} + ${waitsec}`

flock_test() {
	ope1=$1
	ope2=$2
	okElapse=`expr $3 + 1`

	start=`date '+%s'`
	${testbin} ${testfile} ${ope1} ${waitsec} &
	${testbin} ${testfile} ${ope2} ${waitsec}
	wait $!
	end=`date '+%s'`
	elapse=`expr ${end} - ${start}`
	
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	if [ ${elapse} -ge ${okElapse} ]; then
		echo "elapse $elapse sec, must be $okElapse";
		exit $exit_fail
	fi
}

# flock #1
{
	flock_test s s ${sharesec}
}

# flock #2
{
	flock_test s e ${exsec}
}

# flock #3
{
	flock_test e s ${exsec}
}

# flock #4
{
	flock_test e e ${exsec}
}
