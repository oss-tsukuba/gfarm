#!/bin/sh

source ./lib/funcs.sh

_ret=1
fail_flag=0

# main
# 4-1-1
run_test "4-1-1" \
    "../tls-test -s --allow_no_crl --tls_certificate_file ../A/server/server.crt \
    --tls_key_file ../A/server/server.key \
    --tls_ca_certificate_path ../A/cacerts_all --once" \
    "../tls-test --allow_no_crl --tls_ca_certificate_path ../A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 4-2-1
run_test "4-2-1" \
    "../tls-test -s --allow_no_crl --tls_certificate_file ../A/server/server.crt \
    --tls_key_file ../A/server/server.key \
    --tls_ca_certificate_path ../A/cacerts_all --once" \
    "../tls-test --allow_no_crl --tls_ca_certificate_path ../B/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 4-3-1
run_test "4-3-1" \
    "../tls-test -s --allow_no_crl --tls_certificate_file ../A/server/server_cat_1.crt \
    --tls_key_file ../A/server/server.key \
    --tls_ca_certificate_path ../A/cacerts_all --once" \
    "../tls-test --allow_no_crl --tls_ca_certificate_path ../A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 4-4-1
run_test "4-4-1" \
    "../tls-test -s --allow_no_crl --tls_certificate_file ../A/server/server_cat_1.crt \
    --tls_key_file ../A/server/server.key \
    --tls_ca_certificate_path ../A/cacerts_root --once" \
    "../tls-test --allow_no_crl --tls_ca_certificate_path ../A/cacerts_root"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then
    _ret=0
fi

exit ${_ret}
