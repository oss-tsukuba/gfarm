#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`

source ${TOP_DIR}/tools/testscripts/lib/funcs.sh

_ret=1
test_id="7-1"
result=""
expected_result=""
expected_result_csv="${TOP_DIR}/tools/testscripts/expected-test-result.csv"
key_update_num=0
ENV_DIR="${TOP_DIR}/gfarm_environment"

ulimit -a | grep stack | grep unlimited > /dev/null
if [ $? -ne 0 ]; then
    ulimit -s unlimited
fi

# 7-1
${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
    --buf_size 68157440 --tls_key_update 16777216 --once > /dev/null 2>&1 &

while :
do
    netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        break
    fi
done

key_update_num=`${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/client/client.crt \
    --tls_key_file ${ENV_DIR}/A/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
    --buf_size 68157440 --tls_key_update 16777216 --debug_level 1 2>&1 | grep "key updatted" | wc -l`
result=$?

expected_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $2}' | sed 's:\r$::'`
if [ "${result}" = "${expected_result}" -a ${key_update_num} -eq 16 ]; then
    echo "${test_id}: OK"
    _ret=0
else
    echo "${test_id}: NG"
fi

exit ${_ret}
