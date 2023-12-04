#!/bin/sh

# argment:
#   no argument: do failover manually
#   "auto"     : do failover automatically

autotest=$1

: ${srcdir:=.}

. ${srcdir}/env.sh

${srcdir}/cleanup.sh
${srcdir}/setup.sh

echo -n > failed-list
echo -n > log

npass=0
nfail=0

while read type; do
	if [ "$type" = "" ]; then
		continue
	fi
	tmp=`echo "$type" | cut -c1-1`
	if [ "$tmp" = "#" ]; then
		continue
	fi
	types="$types $type"
done < ${srcdir}/test-list

for type in $types; do
	if [ "$autotest" = "auto" ]; then
		echo "================================" >> log 2>&1
		${srcdir}/test-launch.sh $type $autotest >> log 2>&1
	else
		echo "================================"
		${srcdir}/test-launch.sh $type $autotest
	fi
	if [ "$?" = "0" ]; then
		result=PASS
		npass=`expr $npass + 1`
	else
		echo $type >> failed-list
		result="*FAIL*"
		nfail=`expr $nfail + 1`
	fi

	if [ "$autotest" = "auto" ]; then
		printf "%-30s ... %s\n" $type $result
	fi
	if [ "$SLEEP" != "" ]; then
		sleep $SLEEP
	fi
done

echo ""
echo "PASS: $npass  FAIL: $nfail"

${srcdir}/cleanup.sh

if [ "$nfail" != "0" ]; then
    EXIT_CODE=1
else
    EXIT_CODE=0
fi
exit $EXIT_CODE
