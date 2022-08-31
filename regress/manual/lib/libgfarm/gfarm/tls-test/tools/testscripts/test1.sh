#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd "${TOP_DIR}"; pwd`
. "${TOP_DIR}/lib/funcs.sh"
TOP_DIR=`cd "${TOP_DIR}/../../"; pwd`
ENV_DIR="${TOP_DIR}/test_dir"

fail_num=0
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

### main ###
SERVER_A_KEY="${ENV_DIR}/A/server/server.key"
CLIENT_A_KEY="${ENV_DIR}/A/client/client.key"
SERVER_B_KEY="${ENV_DIR}/B/server/server.key"
CLIENT_B_KEY="${ENV_DIR}/B/client/client.key"
A_B_ALL="${ENV_DIR}/A_B/cacerts_all"


## 1-1 ##
run_test "1-1-1" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-2" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_2_1.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-3" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root_1 \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_2.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-4" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-5" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_1.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--build_chain --allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-6" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_all.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-7" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_2_1.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-8" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_2.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root_1 \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-9" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-1-10" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_1.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --build_chain --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi


## 1-2 ##
run_test "1-2-1" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client_cat_all.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-2" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client_cat_3_2_1.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-3" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client_cat_3_2.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-4" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-5" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client_cat_3_1.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-6" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client_cat_all.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-7" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client_cat_3_2_1.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-8" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root_1 \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client_cat_3_2.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-9" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-10" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/B/client/client_cat_3_1.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --build_chain \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-11" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_all.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
        fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-12" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_2_1.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-13" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_2.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-14" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-15" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_1.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-16" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_all.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-17" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_2_1.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-18" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_2.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root_1 \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-19" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-2-20" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_1.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --build_chain --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/B/client/client.crt \
--tls_key_file ${CLIENT_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi


## 1-3 ##
run_test "1-3-1" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-2" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_2_1.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-3" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_2.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-4" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-5" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_1.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-6" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-7" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_2_1.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-8" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root_1 \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_2.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-9" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-10" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_3_1.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --build_chain \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-11" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_all.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-12" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_2_1.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-13" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_2.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-14" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-15" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_1.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-16" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_all.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-17" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_2_1.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-18" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_2.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root_1 \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-19" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-3-20" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_1.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${A_B_ALL} \
--mutual_authentication --build_chain --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${CLIENT_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi


## 1-4 ##
run_test "1-4-1" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_all.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-4-2" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_2_1.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-4-3" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_2.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root_1 \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-4-4" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-4-5" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/A/server/server_cat_1.crt \
--tls_key_file ${SERVER_A_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--build_chain --allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi


## 1-5 ##
run_test "1-5-1" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_all.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-5-2" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_2_1.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-5-3" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_2.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-5-4" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-5-5" \
"${TOP_DIR}/tls-test -s \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_1.crt \
--tls_key_file ${SERVER_B_KEY} \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-1" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all_without_end_entity.crt \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-2" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all_without_end_entity_out_of_order.crt \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-3" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all_without_end_entity_inter_ca_2.crt \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-4" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_all_without_end_entity.crt \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-5" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_all_without_end_entity_out_of_order.crt  \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-6" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_all_without_end_entity_inter_ca_2.crt  \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-7" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_all_without_end_entity.crt \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all_without_end_entity.crt \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-8" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_certificate_chain_file ${ENV_DIR}/B/server/server_cat_all_without_end_entity_out_of_order.crt \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_root \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all_without_end_entity_out_of_order.crt \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_root \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

run_test "1-6-9" \
"${TOP_DIR}/tls-test -s \
--once \
--tls_certificate_file ${ENV_DIR}/B/server/server.crt \
--tls_key_file ${ENV_DIR}/B/server/server.key \
--tls_certificate_chain_file ${ENV_DIR}/B/server/cat_all_without_end_entity_inter_ca_2.crt \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" \
"${TOP_DIR}/tls-test \
--once \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_certificate_chain_file ${ENV_DIR}/A/client/client_cat_all_without_end_entity_inter_ca_2.crt \
--tls_ca_certificate_path ${ENV_DIR}/B/cacerts_all \
--mutual_authentication \
--allow_no_crl \
--verify_only" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

exit ${fail_num}
