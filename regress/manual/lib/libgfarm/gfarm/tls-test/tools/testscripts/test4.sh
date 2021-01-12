#!/bin/sh

source ./lib/funcs.sh

_ret=1
fail_flag=0
topdir="../.."
envdir="../../gfarm_environment"

# main
# 4-1
run_test "4-1" \
    "${topdir}/tls-test -s --allow_no_crl --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 4-2
run_test "4-2" \
    "${topdir}/tls-test -s --allow_no_crl --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --tls_ca_certificate_path ${envdir}/B/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 4-3
run_test "4-3" \
    "${topdir}/tls-test -s --allow_no_crl --tls_certificate_file ${envdir}/A/server/server_cat_1.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all --once" \
    "${topdir}/tls-test --allow_no_crl --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 4-4
run_test "4-4" \
    "${topdir}/tls-test -s --allow_no_crl --tls_certificate_file ${envdir}/A/server/server_cat_1.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_root --once" \
    "${topdir}/tls-test --allow_no_crl --tls_ca_certificate_path ${envdir}/A/cacerts_root"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then
    _ret=0
fi

exit ${_ret}
