#!/bin/sh

puts_error() {
	echo "ERR: $1" 1>&2
}

# run tests.
#
# params:
#   test_id:                 Test ID.
#   server_cmd:              Command to execute for server.
#   client_cmd:              Command to execute for client.
#   debug_flag:              Debug flag.
#   expected_server_log_reg: Expected server log regular expression.
#
# return:
#   0: Test succeeded.
#   1: Test failed.
#
run_test() {
	test_id=$1
	server_cmd=$2
	client_cmd=$3
	debug_flag=$4
	expected_server_log_reg=${5:-""}

	top_dir=`dirname $0`
	top_dir=`cd "${top_dir}/../../"; pwd`
	env_dir="${top_dir}/test_dir"
	_r=1
	server_exitstatus=""
	expected_server_result=""
	expected_client_result=""
	expected_result_csv="`dirname $0`/expected-test-result.csv"
	result_server=0
	s_exit_file="server_exit_status.txt"
	tmp_output_file="/tmp/.tls_test_output"

	# Save server log to /tmp.
	if [ -n "${expected_server_log_reg}" ]; then
		output_file=$tmp_output_file
	else
		output_file=/dev/null
	fi

	sh -c "rm -f ./${s_exit_file}; sync; ${server_cmd} > $output_file 2>&1; \
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
		rm -f ${tmp_output_file}
		echo "server fatal fail"
		case ${result_server} in
			2) echo "tls_context error";;
			3) echo "bind error";;
		esac
		echo "${test_id}:	FAIL"
		return ${_r}
	fi

	sh -c "${client_cmd} > /dev/null 2>&1"
	client_exitstatus=$?

	if [ ${client_exitstatus} -ne 2 -a ${client_exitstatus} -ne 3 ]; then
		wait_server ${child_pid}
	fi
	if [ -s ./${s_exit_file} ]; then
		server_exitstatus=`cat ./${s_exit_file}`
	fi

	if [ ${debug_flag} -eq 1 ]; then
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
		if [ -n "${expected_server_log_reg}" ]; then
			# Check server log.
			log=`grep -E "${expected_server_log_reg}" $tmp_output_file`
			if [ ${debug_flag} -eq 1 ]; then
				echo "actual server log:'${log}'"
			fi

			if [ -n "${log}" ]; then
				echo "${test_id}:	PASS"
				_r=0
			else
				echo "${test_id}:	FAIL"
			fi
		else
		    echo "${test_id}:	PASS"
		    _r=0
		fi
	else
		echo "${test_id}:	FAIL"
	fi

	if [ ${client_exitstatus} -eq 2 -o ${client_exitstatus} -eq 3 ]; then
		shutdown_server ${child_pid}
	fi

	rm -f ./${s_exit_file}
	rm -f ${tmp_output_file}

	return ${_r}
}

# run tests for single
#
# params:
#   test_id:    Test ID.
#   type:       Type of target (client or server).
#   cmd:        Command to execute.
#   debug_flag: Debug flag.
#
# return:
#   0: Test succeeded.
#   1: Test failed.
#
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
