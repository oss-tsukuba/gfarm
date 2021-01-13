#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`
source "${TOP_DIR}/tools/testscripts/lib/funcs.sh"
ENV_DIR="${TOP_DIR}/gfarm_environment"


CERT_DIR="${ENV_DIR}/cert_store"


## result_check func ##
result_check_func() {
    expected_result=`grep -w "$1" "${TOP_DIR}/tools/testscripts/result.csv" | \
                     awk -F "," '{print $2}' | sed 's:\r$::'`
    if [ $2 -eq $expected_result ]; then
        echo "$1:OK"
    else
        echo "$1:NG"
    fi  
    return 0
}


## 10-1 ##
test_id="10-1"

"${TOP_DIR}"/tls-test -s --mutual_authentication \
--tls_certificate_file "${CERT_DIR}/A/server/server.crt" \
--tls_key_file "${CERT_DIR}/A/server/server.key" \
--tls_ca_certificate_path "${CERT_DIR}/A/cacerts_all" --allow_no_crl &
result=$?
result_check_func ${test_id} ${result}
kill -9 $!


## 10-2 ##
run_test "10-2" \
"${TOP_DIR}/tls-test -s --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/server/server.crt \
--tls_key_file ${CERT_DIR}/A/server/server.key \
--tls_ca_certificate_path ${CERT_DIR}/B/cacerts_all --allow_no_crl --once" \
"${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/B/client/client.crt \
--tls_key_file ${CERT_DIR}/B/client/client.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl"


## 10-3 ##
test_id="10-3"

"${TOP_DIR}"/tls-test -s --mutual_authentication \
--tls_certificate_file "${CERT_DIR}/A/server/server.crt" \
--tls_key_file "${CERT_DIR}/A/server/server.key" \
--tls_ca_certificate_path "${CERT_DIR}/A/cacerts_all" \
--tls_client_ca_certificate_path "${CERT_DIR}/B/cacerts_all" --allow_no_crl &
result=$?
result_check_func ${test_id} ${result}
kill -9 $!


exit 0
