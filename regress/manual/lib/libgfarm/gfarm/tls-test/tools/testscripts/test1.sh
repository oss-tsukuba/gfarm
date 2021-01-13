#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd "${TOP_DIR}"; pwd`
TOP_DIR=`cd "${TOP_DIR}/../../"; pwd`
ENV_DIR="${TOP_DIR}/gfarm_environment"

## function ##
base_func_server() {
    if [ "$1" != "/" ]; then
        if [ $5 -ne 0 ]; then
            "${TOP_DIR}"/tls-test -s --tls_certificate_file="$1" \
            --tls_key_file="$3" --tls_ca_certificate_path="$4" \
            --mutual_authentication --allow_no_crl --once &
        else
            "${TOP_DIR}"/tls-test -s --tls_certificate_file="$1" \
            --tls_key_file="$3" --tls_ca_certificate_path="$4" \
            --allow_no_crl --once &
        fi

    else
         if [ $5 -ne 0 ]; then
              if [ $6 -ne 0 ]; then
                  "${TOP_DIR}"/tls-test -s --tls_certificate_chain_file="$2" \
                  --tls_key_file="$3" --tls_ca_certificate_path="$4" \
                  --mutual_authentication --build_chain --allow_no_crl --once &
              else
                  "${TOP_DIR}"/tls-test -s --tls_certificate_chain_file="$2" \
                  --tls_key_file="$3" --tls_ca_certificate_path="$4" \
                  --mutual_authentication --allow_no_crl --once &
              fi
         else
              if [ $6 -ne 0 ]; then
                  "${TOP_DIR}"/tls-test -s --tls_certificate_chain_file="$2" \
                  --tls_key_file="$3" --tls_ca_certificate_path="$4" \
                  --build_chain --allow_no_crl --once &
              else
                  "${TOP_DIR}"/tls-test -s --tls_certificate_chain_file="$2" \
                  --tls_key_file="$3" --tls_ca_certificate_path="$4" \
                  --allow_no_crl --once &
              fi
         fi
    fi
    while :
    do
        netstat -an | grep :12345 | grep LISTEN > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            break
        fi
    done 
    return 0
}

base_func_client() {
    if [ "$1" != "/" ]; then
        "${TOP_DIR}"/tls-test --tls_certificate_file="$1" \
        --tls_key_file="$3" --tls_ca_certificate_path="$4" \
        --mutual_authentication --allow_no_crl
         real_result=$?
    else
        if [ $5 -ne 0 ]; then
            if [ $6 -ne 0 ]; then
                "${TOP_DIR}"/tls-test --tls_certificate_chain_file="$2" \
                --tls_key_file="$3" --tls_ca_certificate_path="$4" \
                --mutual_authentication --build_chain --allow_no_crl
                 real_result=$?
            else
                "${TOP_DIR}"/tls-test --tls_certificate_chain_file="$2" \
                --tls_key_file="$3" --tls_ca_certificate_path="$4" \
                --mutual_authentication --allow_no_crl
                 real_result=$?
            fi
        else
             "${TOP_DIR}"/tls-test --tls_ca_certificate_path="$4" \
             --allow_no_crl
              real_result=$?
        fi
    fi
    expected_result=`grep -w "$7" \
                     "${TOP_DIR}/tools/testscripts/expected-test-result.csv" \
                     | awk -F "," '{print $2}' | sed 's/\r$//'`
    if [ $real_result -eq $expected_result ]; then
        echo "$7:OK"
    else
        echo "$7:NG"
    fi
    return 0
}

### main ###
SERVER_A_KEY="${ENV_DIR}/A/server/server.key"
CLIENT_A_KEY="${ENV_DIR}/A/client/client.key"
SERVER_B_KEY="${ENV_DIR}/B/server/server.key"
CLIENT_B_KEY="${ENV_DIR}/B/client/client.key"
A_B_ALL="${ENV_DIR}/A_B/cacerts_all"


## 1-1 ##
base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                 "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_all.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0 1-1-1

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_2_1.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0 1-1-2

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_root_1" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_2.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0 1-1-3

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0 1-1-4

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_1.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 1 1-1-5

base_func_server "/" "${ENV_DIR}/A/server/server_cat_all.crt" \
                 "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/A/cacerts_root" 1 0 1-1-6

base_func_server "/" "${ENV_DIR}/A/server/server_cat_2_1.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/A/cacerts_root" 1 0 1-1-7

base_func_server "/" "${ENV_DIR}/A/server/server_cat_2.crt" \
                 "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/A/cacerts_root_1" 1 0 1-1-8

base_func_server "/" "${ENV_DIR}/A/server/server.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/A/cacerts_all" 1 0 1-1-9

base_func_server "/" "${ENV_DIR}/A/server/server_cat_1.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 1 1
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/A/cacerts_root" 1 0 1-1-10

## 1-2 ##
base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client_cat_all.crt" \
                 "${CLIENT_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0 1-2-1

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client_cat_3_2_1.crt" \
                 "${CLIENT_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0 1-2-2

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client_cat_3_2.crt" \
                 "${CLIENT_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0 1-2-3

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client.crt" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_all" 1 0 1-2-4

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client_cat_3_1.crt" \
                 "${CLIENT_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0 1-2-5

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client_cat_all.crt" \
                 "${CLIENT_B_KEY}" "${A_B_ALL}" 1 0 1-2-6

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client_cat_3_2_1.crt" \
                 "${CLIENT_B_KEY}" "${A_B_ALL}" 1 0 1-2-7

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_root_1" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client_cat_3_2.crt" \
                 "${CLIENT_B_KEY}" "${A_B_ALL}" 1 0 1-2-8

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client.crt" "${CLIENT_B_KEY}" \
                 "${A_B_ALL}" 1 0 1-2-9

base_func_server "${ENV_DIR}/A/server/server.crt" "/" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/B/client/client_cat_3_1.crt" \
                 "${CLIENT_B_KEY}" "${A_B_ALL}" 1 1 1-2-10

base_func_server "/" "${ENV_DIR}/A/server/server_cat_all.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_all" 1 0 1-2-11

base_func_server "/" "${ENV_DIR}/A/server/server_cat_2_1.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_all" 1 0 1-2-12

base_func_server "/" "${ENV_DIR}/A/server/server_cat_2.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_all" 1 0 1-2-13

base_func_server "/" "${ENV_DIR}/A/server/server.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_all" 1 0 1-2-14

base_func_server "/" "${ENV_DIR}/A/server/server_cat_1.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_all" 1 0 1-2-15

base_func_server "/" "${ENV_DIR}/A/server/server_cat_all.crt" \
                  "${SERVER_A_KEY}" "${A_B_ALL}" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_root" 1 0 1-2-16

base_func_server "/" "${ENV_DIR}/A/server/server_cat_2_1.crt" \
                  "${SERVER_A_KEY}" "${A_B_ALL}" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_root" 1 0 1-2-17

base_func_server "/" "${ENV_DIR}/A/server/server_cat_2.crt" \
                  "${SERVER_A_KEY}" "${A_B_ALL}" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_root_1" 1 0 1-2-18

base_func_server "/" "${ENV_DIR}/A/server/server.crt" \
                  "${SERVER_A_KEY}" "${A_B_ALL}" 1 0
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_all" 1 0 1-2-19

base_func_server "/" "${ENV_DIR}/A/server/server_cat_1.crt" \
                  "${SERVER_A_KEY}" "${A_B_ALL}" 1 1
base_func_client "${ENV_DIR}/B/client/client.crt" "/" "${CLIENT_B_KEY}" \
                 "${ENV_DIR}/A/cacerts_root" 1 0 1-2-20

## 1-3 ##
base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_all.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0 1-3-1

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_2_1.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0 1-3-2

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_2.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0 1-3-3

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client.crt" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_all" 1 0 1-3-4

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_1.crt" \
                 "${CLIENT_A_KEY}" "${ENV_DIR}/B/cacerts_all" 1 0 1-3-5

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_all.crt" \
                 "${CLIENT_A_KEY}" "${A_B_ALL}" 1 0 1-3-6

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_2_1.crt" \
                 "${CLIENT_A_KEY}" "${A_B_ALL}" 1 0 1-3-7

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_root_1" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_2.crt" \
                 "${CLIENT_A_KEY}" "${A_B_ALL}" 1 0 1-3-8

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client.crt" "${CLIENT_A_KEY}" \
                 "${A_B_ALL}" 1 0 1-3-9

base_func_server "${ENV_DIR}/B/server/server.crt" "/" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_root" 1 0
base_func_client "/" "${ENV_DIR}/A/client/client_cat_3_1.crt" \
                 "${CLIENT_A_KEY}" "${A_B_ALL}" 1 1 1-3-10

base_func_server "/" "${ENV_DIR}/B/server/server_cat_all.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_all" 1 0 1-3-11

base_func_server "/" "${ENV_DIR}/B/server/server_cat_2_1.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_all" 1 0 1-3-12

base_func_server "/" "${ENV_DIR}/B/server/server_cat_2.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_all" 1 0 1-3-13

base_func_server "/" "${ENV_DIR}/B/server/server.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_all" 1 0 1-3-14

base_func_server "/" "${ENV_DIR}/B/server/server_cat_1.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/A/cacerts_all" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_all" 1 0 1-3-15

base_func_server "/" "${ENV_DIR}/B/server/server_cat_all.crt" \
                  "${SERVER_B_KEY}" "${A_B_ALL}" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_root" 1 0 1-3-16

base_func_server "/" "${ENV_DIR}/B/server/server_cat_2_1.crt" \
                  "${SERVER_B_KEY}" "${A_B_ALL}" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_root" 1 0 1-3-17

base_func_server "/" "${ENV_DIR}/B/server/server_cat_2.crt" \
                  "${SERVER_B_KEY}" "${A_B_ALL}" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_root_1" 1 0 1-3-18

base_func_server "/" "${ENV_DIR}/B/server/server.crt" \
                  "${SERVER_B_KEY}" "${A_B_ALL}" 1 0
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_all" 1 0 1-3-19

base_func_server "/" "${ENV_DIR}/B/server/server_cat_1.crt" \
                  "${SERVER_B_KEY}" "${A_B_ALL}" 1 1
base_func_client "${ENV_DIR}/A/client/client.crt" "/" "${CLIENT_A_KEY}" \
                 "${ENV_DIR}/B/cacerts_root" 1 0 1-3-20

## 1-4 ##
base_func_server "/" "${ENV_DIR}/A/server/server_cat_all.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/A/cacerts_root" 0 0 1-4-1

base_func_server "/" "${ENV_DIR}/A/server/server_cat_2_1.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/A/cacerts_root" 0 0 1-4-2

base_func_server "/" "${ENV_DIR}/A/server/server_cat_2.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/A/cacerts_root_1" 0 0 1-4-3

base_func_server "/" "${ENV_DIR}/A/server/server.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/A/cacerts_all" 0 0 1-4-4

base_func_server "/" "${ENV_DIR}/A/server/server_cat_1.crt" \
                  "${SERVER_A_KEY}" "${ENV_DIR}/A/cacerts_all" 0 1
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/A/cacerts_root" 0 0 1-4-5

## 1-5 ##
base_func_server "/" "${ENV_DIR}/B/server/server_cat_all.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/B/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/B/cacerts_all" 0 0 1-5-1

base_func_server "/" "${ENV_DIR}/B/server/server_cat_2_1.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/B/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/B/cacerts_all" 0 0 1-5-2

base_func_server "/" "${ENV_DIR}/B/server/server_cat_2.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/B/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/B/cacerts_all" 0 0 1-5-3

base_func_server "/" "${ENV_DIR}/B/server/server.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/B/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/B/cacerts_all" 0 0 1-5-4

base_func_server "/" "${ENV_DIR}/B/server/server_cat_1.crt" \
                  "${SERVER_B_KEY}" "${ENV_DIR}/B/cacerts_all" 0 0
base_func_client "/" "/" "/" \
                 "${ENV_DIR}/B/cacerts_all" 0 0 1-5-5


exit 0
