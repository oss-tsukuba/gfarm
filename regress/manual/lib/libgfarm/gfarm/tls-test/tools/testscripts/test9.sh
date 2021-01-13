#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd "${TOP_DIR}"; pwd`
TOP_DIR=`cd "${TOP_DIR}/../../"; pwd`
source "${TOP_DIR}/tools/testscripts/lib/funcs.sh"
ENV_DIR="${TOP_DIR}/gfarm_environment"

FAIL_FLAG=0
CERT_DIR="${ENV_DIR}/permission_cert"


## result_check func ##

result_check_func() {
    expected_result=`grep -E "^$1" "${TOP_DIR}/tools/testscripts/expected-test-result.csv" | awk -F "," '{print $2}' | sed 's:\r$::'`
    if [ $2 -eq $expected_result ]; then
        echo "$1:OK"
    else
        echo "$1:NG"
        FAIL_FLAG=1
    fi  
    return 0
}

## 9-1 ##
test_id="9-1"

"${TOP_DIR}"/tls-test -s --mutual_authentication \
--tls_certificate_file "${CERT_DIR}/A/server/server.crt" \
--tls_key_file "${CERT_DIR}/A/server/server.key" \
--tls_ca_certificate_path "${CERT_DIR}/A/cacerts_all" --allow_no_crl &
result=$?
while :
    do
        netstat -an | grep :12345 | grep LISTEN > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            break
        fi
    done
result_check_func ${test_id} ${result}
kill -9 $!
while :
do
   kill -0 $!
   if [ $? -ne 0 ]; then
       break
   fi  
done


## 9-2 ##
run_test "9-2" \
"${TOP_DIR}/tls-test -s --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/server/server.crt \
--tls_key_file ${CERT_DIR}/A/server/server.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl --once" \
"${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl"
if [ $? -ne 0 ]; then
    FAIL_FLAG=1
fi

if [ ${FAIL_FLAG} -eq 0 ]; then
    exit 0
else
    exit 1
fi
