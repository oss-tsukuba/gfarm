#!/bin/sh

gen_command_line() {
    if test ! -z "${1}"; then
        cmd=`echo ${1} | awk '{ print $1 }'`
	file ${cmd} | grep 'POSIX shell script' > /dev/null 2>&1
	if test $? -eq 0; then
	    dir=`dirname ${cmd}`
	    newcmd=`echo ${cmd} | sed "s:${dir}::"`
	    newcmd="${dir}/.libs/${newcmd}"
	    echo "${1}" | sed "s:${cmd}:${newcmd}:"
	else
	    echo "${1}"
	fi
	return 0
    fi
    echo "true"
    return 1
}

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
# enhancement for valgrind check:
#   __RUN_VALGRIND__:
#	An enviromnment variable to determine run valgrind or not
#
#   __VALGRIND_NO_SUPPRESS__
#	An enviromnment variable for valgrind not use option "--suppressions=./suppressed_funcs"
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

	do_valg=0
	valgcmd=""
	if [ ! -z "${__RUN_VALGRIND__}" ]; then
	    do_valg=1
	    valgcmd="valgrind --leak-check=full --leak-resolution=high --show-leak-kinds=all --gen-suppressions=all"
	    if [ -z "${__VALGRIND_NO_SUPPRESS__}" -a -r "./suppressed_funcs" ]; then
		valgcmd="${valgcmd} --suppressions=./suppressed_funcs"
	    fi
	fi

	no_out=0
	if [ ${do_valg} -eq 1 ]; then
	    client_output_file="${test_id}-cli.out"
	    server_output_file="${test_id}-srv.out"
	else
	    # Save server log to /tmp.
	    if [ -n "${expected_server_log_reg}" ]; then
		client_output_file="/dev/null"
		server_output_file="/tmp/.tls_test_output"
	    else
		client_output_file="/dev/null"
		server_output_file="/dev/null"
		no_out=1
	    fi
	fi

	server_cmd=`gen_command_line "${server_cmd}"`
	sh -c "rm -f ./${s_exit_file}; sync; \
		${valgcmd} ${server_cmd} > \"${server_output_file}\" 2>&1; \
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
		rm -f "./${s_exit_file}"
		if [ ${no_out} -eq 0 -a ${do_valg} -eq 0 ]; then
		    rm -f "${server_output_file}"
		fi
		echo "server fatal fail"
		case ${result_server} in
			2) echo "tls_context error";;
			3) echo "bind error";;
		esac
		echo "${test_id}:	FAIL"
		return ${_r}
	fi

	client_cmd=`gen_command_line "${client_cmd}"`
	sh -c "${valgcmd} ${client_cmd} > \"${client_output_file}\" 2>&1"
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
			log=`grep -E "${expected_server_log_reg}" "${server_output_file}"`
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

	if [ ${do_valg} -eq 1 ]; then
	    srvwarn=`grep tls_funcs.h "${server_output_file}" | wc -l`
	    cliwarn=`grep tls_funcs.h "${client_output_file}" | wc -l`
	    echo "		server leaks: ${srvwarn}"
	    echo "		client leaks: ${cliwarn}"
	fi

	rm -f ./${s_exit_file}
	if [ ${no_out} -eq 0 -a ${do_valg} -eq 0 ]; then
	    rm -f "${server_output_file}"
	fi

	return ${_r}
}

# run tests for single.
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
    type=$1
    test_id=$2
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

# run tests for client.
#
# params:
#   test_id:    Test ID.
#   cmd:        Command to execute.
#   debug_flag: Debug flag.
#
# return:
#   0: Test succeeded.
#   1: Test failed.
#
run_test_for_client() {
    run_test_for_single "client" "$@"
    return $?
}

# run tests for server.
#
# params:
#   test_id:    Test ID.
#   cmd:        Command to execute.
#   debug_flag: Debug flag.
#
# return:
#   0: Test succeeded.
#   1: Test failed.
#
run_test_for_server() {
    run_test_for_single "server" "$@"
    return $?
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
