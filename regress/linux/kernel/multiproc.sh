#!/bin/sh

. ./regress.conf
. ./linux/kernel/initparams.sh

n=100

# multiproc #1
{
		sh -x ./linux/kernel/manydir.sh mkdir ${MOUNTPOINT}/A ${n} &
		sh -x ./linux/kernel/manydir.sh mkdir ${MOUNTPOINT}/B ${n}
		wait $!

		ls ${MOUNTPOINT}
		sleep 3
}

# multiproc #2
{
		sh -x ./linux/kernel/manydir.sh rmdir ${MOUNTPOINT}/A ${n} &
		sh -x ./linux/kernel/manydir.sh rmdir ${MOUNTPOINT}/B ${n}
		wait $!

		ls ${MOUNTPOINT}
		sleep 3
}
