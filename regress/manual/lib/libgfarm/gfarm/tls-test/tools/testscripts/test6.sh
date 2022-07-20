#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`

. ${TOP_DIR}/tools/testscripts/lib/funcs.sh

ENV_DIR="${TOP_DIR}/test_dir/verify_chain_path"
debug_flag=0
fail_num=0

# funcs.
usage(){
	cat << EOS >&2
Usage:

	OPTION:
		-d			Debug flag
		-h			Help
EOS
exit 0
}

# test funcs.

# 6-1
test_6_1() {
    run_test "6-1" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-2
test_6_2() {
    run_test "6-2" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/A/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-3
test_6_3() {
    run_test "6-3" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/C/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-4
test_6_4() {
    run_test "6-4" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/A/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-5
test_6_5() {
    run_test "6-5" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-6
test_6_6() {
    run_test "6-6" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/C/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-7
test_6_7() {
    run_test "6-7" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/A/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-8
test_6_8() {
    run_test "6-8" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/A/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-9
test_6_9() {
    run_test "6-9" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/C/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/C/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}


# 6-10
test_6_10() {
    run_test "6-10" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server/server.crt \
        --tls_key_file ${ENV_DIR}/A/server/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client_under_inter_ca_4/client.crt \
        --tls_key_file ${ENV_DIR}/B/client_under_inter_ca_4/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all_under_inter_ca_4 \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_on \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-11
test_6_11() {
    run_test "6-11" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server_under_inter_ca_4/server.crt \
        --tls_key_file ${ENV_DIR}/A/server_under_inter_ca_4/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all_under_inter_ca_4 \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client/client.crt \
        --tls_key_file ${ENV_DIR}/B/client/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/A/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}

# 6-12
test_6_12() {
    run_test "6-12" \
        "${TOP_DIR}/tls-test -s \
        --tls_certificate_file ${ENV_DIR}/A/server_under_inter_ca_4/server.crt \
        --tls_key_file ${ENV_DIR}/A/server_under_inter_ca_4/server.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all_under_inter_ca_4 \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/B/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        "${TOP_DIR}/tls-test \
        --tls_certificate_file ${ENV_DIR}/B/client_under_inter_ca_4/client.crt \
        --tls_key_file ${ENV_DIR}/B/client_under_inter_ca_4/client.key \
        --tls_ca_certificate_path ${ENV_DIR}/A_B/cacerts_all_under_inter_ca_4 \
        --tls_ca_peer_verify_chain_path ${ENV_DIR}/A/cacerts_all \
        --once \
        --allow_no_crl \
        --build_chain \
        --verify_only \
        --mutual_authentication" \
        ${debug_flag}

    if [ $? -ne 0 ]; then
	    fail_num=`expr ${fail_num} + 1`
    fi
}


# main.

## Opts. ##
while getopts d OPT; do
	case ${OPT} in
		d) debug_flag=1;;
		h) usage;;
		*) usage;;
	esac
done
shift `expr $OPTIND - 1`

test_6_1
test_6_2
test_6_3
test_6_4
test_6_5
test_6_6
test_6_7
test_6_8
test_6_9
test_6_10
test_6_11
test_6_12

exit ${fail_num}
