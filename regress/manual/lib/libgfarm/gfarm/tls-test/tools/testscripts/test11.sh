#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`
source "${TOP_DIR}/tools/testscripts/lib/funcs.sh"
ENV_DIR="${TOP_DIR}/gfarm_environment/permission_crl_dir"

FAIL_FLAG=0
server_fail_flag=0

server_exitstatus_file="${TOP_DIR}/exitstatus.txt"
logfile="${TOP_DIR}/testfile.log"
expected_result_csv="`dirname $0`/expected-test-result.csv"

## 11-1 ##
test_id="11-1"

rm -f ${logfile}
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${ENV_DIR}/A/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--tls_ca_revocation_path ${ENV_DIR}/B/crls/client/root/ > ${logfile} 2>&1 &
while :
    do
        kill -0 $!
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
    kill -9 $!
    while :
    do
        kill -0 $!
        if [ $? -ne 0 ]; then
            break
        fi  
    done

    cat ${logfile} | grep "warning" > /dev/null 2>&1
    if [ $? -ne 0 ]; then
        echo "${test_id}: OK"
    else
        echo "${test_id}: NG"
        FAIL_FLAG=1
    fi
else
    puts_error "fail to run server."
    echo "${test_id}: NG"
    FAIL_FLAG=1 
fi

## 11-2 ##
test_id="11-2"

rm -f ${logfile}
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${ENV_DIR}/A/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--tls_ca_revocation_path ${ENV_DIR}/B/crls/client/root_bad_permissions > ${logfile} 2>&1 &
while :
    do
        kill -0 $!
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
    kill -9 $!
    while :
    do
        kill -0 $!
        if [ $? -ne 0 ]; then
            break
        fi  
    done

    cat ${logfile} | grep "warning" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "${test_id}: OK"
    else
        echo "${test_id}: NG"
        FAIL_FLAG=1
    fi
else
    puts_error "fail to run server."
    echo "${test_id}: NG"
    FAIL_FLAG=1 
fi

## 11-3 ##
test_id="11-3"

sh -c "rm -f ${server_exitstatus_file}; \
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all --once > /dev/null 2>&1; \
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
    rm -f ${logfile}
    ${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key  \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/A/crls/server/root/ > ${logfile} 2>&1
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
        FAIL_FLAG=1
    else
        server_exitstatus=`cat ${server_exitstatus_file}`
        expected_server_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $2}' | sed 's:\r$::'`
        expected_client_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $3}' | sed 's:\r$::'`
        cat ${logfile} | grep "warning" > /dev/null 2>&1
        if [ $? -ne 0 -a "${server_exitstatus}" = "${expected_server_result}" -a "${client_exitstatus}" = "${expected_client_result}" ]; then
            echo "${test_id}: OK"
        else
            echo "${test_id}: NG"
            FAIL_FLAG=1
        fi
    fi
else
    puts_error "fail to run server."
    echo "${test_id}: NG"
    FAIL_FLAG=1 
fi

## 11-4 ##
test_id="11-4"

sh -c "rm -f ${server_exitstatus_file}; \
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all --once > /dev/null 2>&1; \
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
    rm -f ${logfile}
    ${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key  \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/A/crls/server/root_bad_permissions > ${logfile} 2>&1
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
        FAIL_FLAG=1
    else
        server_exitstatus=`cat ${server_exitstatus_file}`
        expected_server_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $2}' | sed 's:\r$::'`
        expected_client_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $3}' | sed 's:\r$::'`
        cat ${logfile} | grep "warning" > /dev/null 2>&1
        if [ $? -eq 0 -a "${server_exitstatus}" = "${expected_server_result}" -a "${client_exitstatus}" = "${expected_client_result}" ]; then
            echo "${test_id}: OK"
        else
            echo "${test_id}: NG"
            FAIL_FLAG=1
        fi
    fi
else
    puts_error "fail to run server."
    echo "${test_id}: NG"
    FAIL_FLAG=1 
fi

rm -f ${logfile}

if [ ${FAIL_FLAG} -eq 0 ]; then
    exit 0
else
    exit 1
fi
