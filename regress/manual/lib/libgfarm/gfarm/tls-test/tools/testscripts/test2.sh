#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`

source ${TOP_DIR}/tools/testscripts/lib/funcs.sh

_ret=1
fail_flag=0
ENV_DIR="${TOP_DIR}/test_dir/revocation/"
debug_flag=0

usage(){
    cat << EOS >&2
Usage:

    OPTION:
        -d              Debug flag
        -h              Help
EOS
exit 0
}

## Opts. ##
while getopts d OPT; do
    case ${OPT} in
        d) debug_flag=1;;
        h) usage;;
        *) usage;;
    esac
done
shift `expr $OPTIND - 1`

# main
# 2-1-1
run_test "2-1-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/root/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-1-2
run_test "2-1-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/root/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-1-3
run_test "2-1-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/root/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-2-1
run_test "2-2-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-2-2
run_test "2-2-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-2-3
run_test "2-2-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-3-1
run_test "2-3-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-3-2
run_test "2-3-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-3-3
run_test "2-3-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-4-1
run_test "2-4-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-4-2
run_test "2-4-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-4-3
run_test "2-4-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-5-1
run_test "2-5-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1_2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-5-2
run_test "2-5-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1_2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-5-3
run_test "2-5-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1_2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-6-1
run_test "2-6-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1_2_3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-6-2
run_test "2-6-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1_2_3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-6-3
run_test "2-6-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/client1_2_3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-7-1
run_test "2-7-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_1/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-7-2
run_test "2-7-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_1/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-7-3
run_test "2-7-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_1/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-8-1
run_test "2-8-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-8-2
run_test "2-8-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-8-3
run_test "2-8-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_2/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-9-1
run_test "2-9-1" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
    --tls_key_file ${ENV_DIR}/B/client/client.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-9-2
run_test "2-9-2" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client2.crt \
    --tls_key_file ${ENV_DIR}/B/client/client2.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

# 2-9-3
run_test "2-9-3" \
    "${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
    --tls_key_file ${ENV_DIR}/A/server/server.key \
    --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
    --tls_ca_revocation_path ${ENV_DIR}/B/crls/inter_ca_3/ --once" \
    "${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
    --tls_certificate_file ${ENV_DIR}/B/client/client3.crt \
    --tls_key_file ${ENV_DIR}/B/client/client3.key \
    --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
    fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then
    _ret=0
fi

exit ${_ret}
