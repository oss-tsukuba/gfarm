#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`

source ${TOP_DIR}/tools/testscripts/lib/funcs.sh

_ret=1
test_id="7-1"
expected_result_csv="${TOP_DIR}/tools/testscripts/expected-test-result.csv"
key_update_num=0
ENV_DIR="${TOP_DIR}/test_dir"

server_exitstatus_file="${TOP_DIR}/exitstatus.txt"
server_fail_flag=0

ulimit -a | grep stack | grep unlimited > /dev/null
if [ $? -ne 0 ]; then
    ulimit -s unlimited
fi

# 7-1

sh -c "rm -f ${server_exitstatus_file}; \
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${ENV_DIR}/A/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--buf_size 68157440 --tls_key_update 16777216 --once > /dev/null 2>&1; \
echo \$? > ${server_exitstatus_file}" &
server_pid=$!
while :
do
    kill -0 ${server_pid}
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
    key_update_num=`${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/client/client.crt \
    --tls_key_file ${ENV_DIR}/A/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
    --buf_size 68157440 --tls_key_update 16777216 --debug_level 1 2>&1 | grep "key updatted" | wc -l`
    client_exitstatus=$?

    if [ ${client_exitstatus} -eq 2 -o ${client_exitstatus} -eq 3 ]; then
        kill -9 ${server_pid}
        while :
        do
            netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
            if [ $? -ne 0 ]; then
                break
            fi
        done
        echo "${test_id}: NG"
    else
        while :
        do
            sync
            kill -0 ${server_pid} > /dev/null 2>&1
            kill_status=$?
            test -s ${server_exitstatus_file}
            file_status=$?
            if [ ${kill_status} -ne 0 -a ${file_status} -eq 0 ]; then
                server_exitstatus=`cat ${server_exitstatus_file}`
                break
            fi
        done
        expected_server_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $2}' | sed 's:\r$::'`
        expected_client_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $3}' | sed 's:\r$::'`
        if [ ${key_update_num} -eq 16 \
                -a "${server_exitstatus}" = "${expected_server_result}" \
                -a "${client_exitstatus}" = "${expected_client_result}" ]; then
            echo "${test_id}: OK"
            _ret=0
        else
            echo "${test_id}: NG"
        fi
    fi
else
    puts_error "fail to run server."
    echo "${test_id}: NG"
fi

rm -f ${server_exitstatus_file}

exit ${_ret}
