#!/bin/sh

source ./lib/funcs.sh

_ret=1
fail_flag=0
topdir="../.."
envdir="../../gfarm_environment/revocation/"

# main
# 2-1-1
run_test "2-1-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/root/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-1-2
run_test "2-1-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/root/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-1-3
run_test "2-1-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/root/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-2-1
run_test "2-2-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-2-2
run_test "2-2-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-2-3
run_test "2-2-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-3-1
run_test "2-3-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-3-2
run_test "2-3-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-3-3
run_test "2-3-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-4-1
run_test "2-4-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-4-2
run_test "2-4-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-4-3
run_test "2-4-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-5-1
run_test "2-5-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1_2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-5-2
run_test "2-5-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1_2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-5-3
run_test "2-5-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1_2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-6-1
run_test "2-6-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1_2_3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-6-2
run_test "2-6-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1_2_3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-6-3
run_test "2-6-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/client1_2_3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-7-1
run_test "2-7-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_1/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-7-2
run_test "2-7-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_1/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-7-3
run_test "2-7-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_1/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-8-1
run_test "2-8-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-8-2
run_test "2-8-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-8-3
run_test "2-8-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_2/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-9-1
run_test "2-9-1" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client.crt \
    --tls_key_file ${envdir}/B/client/client.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-9-2
run_test "2-9-2" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client2.crt \
    --tls_key_file ${envdir}/B/client/client2.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-9-3
run_test "2-9-3" \
    "${topdir}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/A/server/server.crt \
    --tls_key_file ${envdir}/A/server/server.key \
    --tls_ca_certificate_path ${envdir}/B/cacerts_all \
    --tls_ca_revocation_path ${envdir}/B/crls/inter_ca_3/ --once" \
    "${topdir}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${envdir}/B/client/client3.crt \
    --tls_key_file ${envdir}/B/client/client3.key \
    --tls_ca_certificate_path ${envdir}/A/cacerts_all"

if [ $? -ne 0 ]; then
    fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then
    _ret=0
fi

exit ${_ret}
