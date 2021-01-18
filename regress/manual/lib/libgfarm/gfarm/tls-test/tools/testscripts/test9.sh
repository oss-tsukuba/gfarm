#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd "${TOP_DIR}"; pwd`
TOP_DIR=`cd "${TOP_DIR}/../../"; pwd`
source "${TOP_DIR}/tools/testscripts/lib/funcs.sh"
ENV_DIR="${TOP_DIR}/gfarm_environment"

FAIL_FLAG=0
CERT_DIR="${ENV_DIR}/permission_cert"


## 9-1 ##
run_test "9-1" \
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
