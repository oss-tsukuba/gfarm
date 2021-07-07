#!/bin/sh

puts_error() {
	echo "ERR: $1" 1>&2
}

run_test() {
	top_dir=`dirname $0`
	top_dir=`cd "${top_dir}/../../"; pwd`
	env_dir="${top_dir}/test_dir"
	_r=1
	test_id=$1
	server_exitstatus=""
	expected_server_result=""
	expected_client_result=""
	expected_result_csv="`dirname $0`/expected-test-result.csv"
	result_server=0
	s_exit_file="server_exit_status.txt"

	sh -c "rm -f ./${s_exit_file}; sync; $2 > /dev/null 2>&1; \
		echo \$? > ./${s_exit_file}; sync" &
	child_pid=$!
	while :
	do
		kill -0 ${child_pid} > /dev/null 2>&1
		if [ $? -eq 0 ]; then
			break
		fi
	done

	while :
	do
		sleep 1
		kill -0 ${child_pid} > /dev/null 2>&1
		if [ $? -ne 0 ]; then
			result_server=`cat ./${s_exit_file}`
			break
		fi
		netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
		if [ $? -eq 0 ]; then
			break
		fi
	done

	if [ ${result_server} -ne 0 ]; then
		rm -f ./${s_exit_file}
		echo "server fatal fail"
		case ${result_server} in
			2) echo "tls_context error";;
			3) echo "bind error";;
		esac
		echo "${test_id}:	FAIL"
		return ${_r}
	fi

	sh -c "$3 > /dev/null 2>&1"
	client_exitstatus=$?

	if [ ${client_exitstatus} -ne 2 -a ${client_exitstatus} -ne 3 ]; then
		wait_server ${child_pid}
	fi
	if [ -s ./${s_exit_file} ]; then
		server_exitstatus=`cat ./${s_exit_file}`
	fi

	if [ $4 -eq 1 ]; then
		echo "server:$server_exitstatus"
		echo "client:$client_exitstatus"
	fi

	expected_server_result=`cat ${expected_result_csv} | \
                                grep -E "^${test_id}," | \
                                awk -F "," '{print $2}' | sed 's:\r$::'`
	expected_client_result=`cat ${expected_result_csv} | \
                                grep -E "^${test_id}," | \
                                awk -F "," '{print $3}' | sed 's:\r$::'`

	if [ "${server_exitstatus}" = "${expected_server_result}" \
	     -a "${client_exitstatus}" = "${expected_client_result}" ]; then
		echo "${test_id}:	PASS"
		_r=0
	else
		echo "${test_id}:	FAIL"
	fi

	if [ ${client_exitstatus} -eq 2 -o ${client_exitstatus} -eq 3 ]; then
		shutdown_server ${child_pid}
	fi

	rm -f ./${s_exit_file}

	return ${_r}
}

run_test_for_single() {
    test_id=$1
    type=$2
    cmd=$3
    debug_flag=$4
    expected_result_csv="`dirname $0`/expected-test-result.csv"

    ${cmd} > /dev/null 2>&1
    exitstatus=$?

    if [ ${debug_flag} -eq 1 ]; then
        echo "${type}:${exitstatus}"
    fi
    if [ "x${type}" = "xclient" ]; then
        # client
        column=3
    else
        # server
        column=2
    fi
    expected_result=`grep -E "^${test_id}," ${expected_result_csv} | \
                cut -d ',' -f${column}`
    if [ "x${exitstatus}" = "x${expected_result}" ]; then
        echo "${test_id}:   PASS"
        return 0
    fi

    echo "${test_id}:   FAIL"
    return 1
}

wait_server(){
	while :
	do
		kill -0 $1 > /dev/null 2>&1
		if [ $? -ne 0 ]; then
			break
		fi
	done
}

shutdown_server(){
        kill -9 $1
        while :
        do
                netstat -an | grep LISTEN | grep :12345 \
                        > /dev/null 2>&1
                if [ $? -ne 0 ]; then 
                        break
                fi
        done
}
