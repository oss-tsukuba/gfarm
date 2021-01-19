#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd "${TOP_DIR}"; pwd`
TOP_DIR=`cd "${TOP_DIR}/../../"; pwd`
source "${TOP_DIR}/tools/testscripts/lib/funcs.sh"
ENV_DIR="${TOP_DIR}/test_dir"

FAIL_FLAG=0
CERT_DIR="${ENV_DIR}/permission_private_key"


## 8-1 ##
run_test "8-1" \
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

## 8-2 ##
run_test "8-2" \
"${TOP_DIR}/tls-test -s --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/server/server.crt \
--tls_key_file ${CERT_DIR}/A/server/server.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl --once" \
"${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client_bad_permissions.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl"
if [ $? -ne 0 ]; then
    FAIL_FLAG=1
fi

## 8-3 ##
run_test "8-3" \
"${TOP_DIR}/tls-test -s --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/server/server.crt \
--tls_key_file ${CERT_DIR}/A/server/server.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl --once" \
"${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client_bad_permissions_bad_user.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl"
if [ $? -ne 0 ]; then
    FAIL_FLAG=1
fi

# 8-4 ##
run_test "8-4" \
"${TOP_DIR}/tls-test -s --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/server/server.crt \
--tls_key_file ${CERT_DIR}/A/server/server.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl --once" \
"${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client_bad_permissions_bad_user2.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl"
if [ $? -ne 0 ]; then
    FAIL_FLAG=1
fi

if [ ${FAIL_FLAG} -eq 0 ]; then
    exit 0
else
    exit 1
fi
