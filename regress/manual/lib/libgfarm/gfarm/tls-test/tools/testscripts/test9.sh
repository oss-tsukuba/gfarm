#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`

if [ ! -d "${TOP_DIR}/gfarm_environment/permission_cert" ]; then
    "${TOP_DIR}"/shellscripts/test_data_permission_cert.sh \
     -o "${TOP_DIR}/gfarm_environment"
fi

CERT_DIR="${TOP_DIR}/gfarm_environment/permission_cert"


## result_check func ##
result_check_func() {
    expected_result=`grep -w "$1" "${TOP_DIR}/result.csv" | \
                     awk -F "," '{print $2}' | sed 's:\r$::'`
    if [ $2 -eq $expected_result ]; then
        echo "$1:OK"
    else
        echo "$1:NG"
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
result_check_func ${test_id} ${result}
kill -9 $!


## 9-2 ##
test_id="9-2"

"${TOP_DIR}"/tls-test -s --mutual_authentication \
--tls_certificate_file "${CERT_DIR}/A/server/server.crt" \
--tls_key_file "${CERT_DIR}/A/server/server.key" \
--tls_ca_certificate_path "${CERT_DIR}/A/cacerts_all" --allow_no_crl --once &
while :
do
    netstat -an | grep :12345 | grep LISTEN > /dev/null 2>&1
    if [ $? -eq 0 ]; then
        break
    fi  
done

"${TOP_DIR}"/tls-test --mutual_authentication \
--tls_certificate_file "${CERT_DIR}/A/client/client.crt" \
--tls_key_file "${CERT_DIR}/A/client/client.key" \
--tls_ca_certificate_path  "${CERT_DIR}/A/cacerts_all" --allow_no_crl
result=$?
result_check_func ${test_id} ${result}


exit 0
