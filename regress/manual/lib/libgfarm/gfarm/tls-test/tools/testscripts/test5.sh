#!/bin/sh

source ./lib/funcs.sh

_ret=1
fail_flag=0
topdir="../.."
envdir="../../gfarm_environment"

# main
# 5-1
run_test "5-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/client/client.crt \
    --tls_key_file ${envdir}/A/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 5-2
run_test "5-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 5-3
run_test "5-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/server/server.crt \
    --tls_key_file ${envdir}/B/server/server.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/client/client.crt \
    --tls_key_file ${envdir}/A/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 5-4
run_test "5-4" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server_cat_1.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 5-5
run_test "5-5" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server_cat_1.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_root"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 5-6
run_test "5-6" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client_cat_3_1.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 5-7
run_test "5-7" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client_cat_3_1.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_root"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then
    _ret=0
fi

exit ${_ret}
