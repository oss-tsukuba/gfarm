#!/bin/sh

source ./lib/funcs.sh

_ret=1
topdir="../.."
envdir="../../gfarm_environment"
test_id="7-1"
result=""
expected_result=""
expected_result_csv="expected-test-result.csv"
key_update_num=0

ulimit -a | grep stack | grep unlimited > /dev/null
if [ $? -ne 0 ]; then
    ulimit -s unlimited
fi

# 7-1

${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all \
    --buf_size 68157440 --tls_key_update 16777216 --once > /dev/null 2>&1 &

while :
do
    netstat -an | grep LISTEN | grep :12345 > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        break
    fi
done

key_update_num=`${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/client/client.crt \
    --tls_key_file ${envdir}/A/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all \
    --buf_size 68157440 --tls_key_update 16777216 --debug_level 1 2>&1 | grep "key updatted" | wc -l`
result=$?

expected_result=`cat ${expected_result_csv} | grep -E "^${test_id}" | awk -F "," '{print $2}'`
if [ "${result}" = "${expected_result}" -a ${key_update_num} -eq 16 ]; then
    echo "${test_id}: OK"
    _ret=0
else
    echo "${test_id}: NG"
fi

exit ${_ret}
