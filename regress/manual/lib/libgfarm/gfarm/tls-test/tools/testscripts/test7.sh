#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`

source ${TOP_DIR}/tools/testscripts/lib/funcs.sh

test_id="7-1"
expected_result_csv="${TOP_DIR}/tools/testscripts/expected-test-result.csv"
key_update_num=0
ENV_DIR="${TOP_DIR}/test_dir"

server_exitstatus=""
s_exit_file="${TOP_DIR}/exitstatus.txt"
server_fail_flag=0
debug_flag=0
fail_num=0

usage(){
	cat << EOS >&2
Usage:

	OPTION:
		-d			Debug flag
		-h			Help
EOS
exit 0
}

## Opts. ##
while getopts d OPT; do
	case ${OPT} in
		d) debug_flag=1;;
		h) usage;;
		*) usage;;
	esac
done
shift `expr $OPTIND - 1`

ulimit -a | grep stack | grep unlimited > /dev/null
if [ $? -ne 0 ]; then
	ulimit -s unlimited
fi

# 7-1

sh -c "rm -f ${s_exit_file}; sync; \
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${ENV_DIR}/A/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--buf_size 68157440 --tls_key_update 16777216 --once > /dev/null 2>&1; \
echo \$? > ${s_exit_file}; sync" &
server_pid=$!
while :
do
	kill -0 ${server_pid} > /dev/null 2>&1
	if [ $? -ne 0 ]; then
		server_fail_flag=1
		break
	fi
	netstat -an | grep :12345 | grep LISTEN > /dev/null 2>&1
	if [ $? -eq 0 ]; then
		break
	fi
done

if [ ${server_fail_flag} -ne 1 ]; then
	client_log=`${TOP_DIR}/tls-test \
	--allow_no_crl --mutual_authentication \
	--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
	--tls_key_file ${ENV_DIR}/A/client/client.key \
	--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
	--buf_size 68157440 --tls_key_update 16777216 \
	--debug_level 1 2>&1`
	client_exitstatus=$?

	if [ ${client_exitstatus} -ne 2 -a ${client_exitstatus} -ne 3 ]; then
		key_update_num=`echo "${client_log}" | \
				grep "key updatted" | wc -l`
		wait_server ${server_pid}
	fi
	if [ -s ${s_exit_file} ]; then
                server_exitstatus=`cat ${s_exit_file}`
        fi

	if [ ${debug_flag} -eq 1 ]; then
		echo "server:${server_exitstatus}"
		echo "client:${client_exitstatus}"
	fi

	expected_server_result=`cat ${expected_result_csv} | \
			grep -E "^${test_id}" | \
			awk -F "," '{print $2}' | sed 's:\r$::'`
	expected_client_result=`cat ${expected_result_csv} | \
			grep -E "^${test_id}" | \
			awk -F "," '{print $3}' | sed 's:\r$::'`
	if [ ${key_update_num} -eq 16 \
		-a "${server_exitstatus}" = "${expected_server_result}" \
		-a "${client_exitstatus}" = "${expected_client_result}" ]; then
		echo "${test_id}:	PASS"
	else
		echo "${test_id}:	FAIL"
		fail_num=`expr ${fail_num} + 1`
	fi
else
	puts_error "fail to run server."
	echo "${test_id}:	FAIL"
	fail_num=`expr ${fail_num} + 1`
fi

if [ ${client_exitstatus} -eq 2 -o ${client_exitstatus} -eq 3 ]; then
	shutdown_server ${server_pid}
fi

rm -f ${s_exit_file}

exit ${fail_num}
