#!/bin/sh

. ./regress.conf

dir=gfarm://$GFARM_TEST_MDS2$gftmp

if [ "$GFARM_TEST_MDS2" = "" ]; then
	echo GFARM_TEST_MDS2 is not set
	exit $exit_unsupported
fi

host=${GFARM_TEST_MDS2%:*}
port=${GFARM_TEST_MDS2#*:}
out_host=`gfstatus -P gfarm://$GFARM_TEST_MDS2 | grep "gfmd server name"`
out_host=${out_host#gfmd server name: }
out_port=`gfstatus -P gfarm://$GFARM_TEST_MDS2 | grep "gfmd server port"`
out_port=${out_port#gfmd server port: }

echo "host=$host port=$port out_host=$out_host out_port=$out_port"

if [ "$host" != "$out_host" ]; then
	echo host is not equal to out_host
	exit $exit_fail
fi
if [ "$port" != "$out_port" ]; then
	echo port is not equal to out_port
	exit $exit_fail
fi

exit $exit_pass
