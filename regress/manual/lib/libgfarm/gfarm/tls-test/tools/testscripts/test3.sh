#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`

source ${TOP_DIR}/tools/testscripts/lib/funcs.sh

_ret=1
expected_result_csv="${TOP_DIR}/tools/testscripts/expected-test-result.csv"
ENV_DIR="${TOP_DIR}/test_dir"

server_exitstatus_file="${TOP_DIR}/exitstatus.txt"
server_fail_flag=0
fail_flag=0

# 3-1
test_id="3-1"

sh -c "rm -f ${server_exitstatus_file}; \
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${ENV_DIR}/A/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all --once > /dev/null 2>&1; \
echo \$? > ${server_exitstatus_file}" &
server_pid=$!
while :
do
    sh -c "kill -0 ${server_pid}" > /dev/null 2>&1
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
    echo "Input correct passphrase."
    ${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/client/client.crt \
    --tls_key_file ${ENV_DIR}/A/client/client_encrypted.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all > /dev/null 2>&1
    client_exitstatus=$?

    if [ ${client_exitstatus} -eq 2 -o ${client_exitstatus} -eq 3 ]; then
        ${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
        --tls_certificate_file ${ENV_DIR}/A/client/client.crt \
        --tls_key_file ${ENV_DIR}/A/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all > /dev/null 2>&1
        while :
        do
            netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
            if [ $? -ne 0 ]; then
                break
            fi
        done
        echo "${test_id}: NG"
        fail_flag=1
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
        expected_server_result=`cat ${expected_result_csv} | grep -E "^${test_id}," | awk -F "," '{print $2}' | sed 's:\r$::'`
        expected_client_result=`cat ${expected_result_csv} | grep -E "^${test_id}," | awk -F "," '{print $3}' | sed 's:\r$::'`
        if [ "${server_exitstatus}" = "${expected_server_result}" \
                -a "${client_exitstatus}" = "${expected_client_result}" ]; then
            echo "${test_id}: OK"
        else
            echo "${test_id}: NG"
            fail_flag=1
        fi
    fi
else
    puts_error "fail to run server."
    echo "${test_id}: NG"
    fail_flag=1
fi

# 3-2
test_id="3-2"

sh -c "rm -f ${server_exitstatus_file}; \
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${ENV_DIR}/A/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all --once > /dev/null 2>&1; \
echo \$? > ${server_exitstatus_file}" &
server_pid=$!
while :
do
    sh -c "kill -0 ${server_pid}" > /dev/null 2>&1
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
    echo "Input bad passphrase."
    ${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/client/client.crt \
    --tls_key_file ${ENV_DIR}/A/client/client_encrypted.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all > /dev/null 2>&1
    client_exitstatus=$?

    if [ ${client_exitstatus} -eq 2 -o ${client_exitstatus} -eq 3 ]; then
        ${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
        --tls_certificate_file ${ENV_DIR}/A/client/client.crt \
        --tls_key_file ${ENV_DIR}/A/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all > /dev/null 2>&1
        while :
        do
            netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
            if [ $? -ne 0 ]; then
                break
            fi
        done
        server_exitstatus=4
        expected_server_result=`cat ${expected_result_csv} | grep -E "^${test_id}," | awk -F "," '{print $2}' | sed 's:\r$::'`
        expected_client_result=`cat ${expected_result_csv} | grep -E "^${test_id}," | awk -F "," '{print $3}' | sed 's:\r$::'`
        if [ "${server_exitstatus}" = "${expected_server_result}" \
                -a "${client_exitstatus}" = "${expected_client_result}" ]; then
            echo "${test_id}: OK"
        else
            echo "${test_id}: NG"
            fail_flag=1
        fi
    else
        echo "${test_id}: NG"
        fail_flag=1
    fi
else
    puts_error "fail to run server."
    echo "${test_id}: NG"
    fail_flag=1
fi

# 3-3
run_test "3-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/client/client.crt \
    --tls_key_file ${ENV_DIR}/A/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all"

if [ $? -ne 0 ]; then 
    fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then 
    _ret=0  
fi

rm -f ${server_exitstatus_file}

exit ${_ret}
