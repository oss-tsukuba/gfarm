#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

# hardlink #1
{
	linksrc=${MOUNTPOINT}/link.src
	linkdst=${MOUNTPOINT}/link.dst
	
	rm -f ${linksrc} ${linkdst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	touch ${linksrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	stat ${linksrc} > ${TMPFILE}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	line=`head -3 ${TMPFILE} | tail -1`
	if ! expr "$line" : ".*Links: 1" > /dev/null; then
		echo "Links is not 1"
		exit $exit_fail
	fi
	
	ln ${linksrc} ${linkdst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	stat ${linksrc} > ${TMPFILE}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	line=`head -3 ${TMPFILE} | tail -1`
	if ! expr "$line" : ".*Links: 2" > /dev/null; then
		echo "Links is not 1"
		exit $exit_fail
	fi

	stat ${linkdst} > ${TMPFILE}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	line2=`head -3 ${TMPFILE} | tail -1`
	if [ "$line" != "$line2" ]; then
		echo "Inode/Links is not equal"
		exit $exit_fail
	fi
}

# hardlink #2
{
	rm ${linkdst}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	
	stat ${linkdst}
	if [ $? == 0 ]; then
		exit $exit_fail
	fi
	
	stat ${linksrc} > ${TMPFILE}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
	line=`head -3 ${TMPFILE} | tail -1`
	if ! expr "$line" : ".*Links: 1" > /dev/null; then
		echo "Links is not 1"
		exit $exit_fail
	fi
	
	rm ${linksrc}
	if [ $? != 0 ]; then
		exit $exit_fail
	fi
}
