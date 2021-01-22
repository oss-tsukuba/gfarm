#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`
source "${TOP_DIR}/tools/testscripts/lib/funcs.sh"
ENV_DIR="${TOP_DIR}/test_dir"

fail_num=0
CERT_DIR="${ENV_DIR}/cert_store"
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


## 10-1 ##
run_test "10-1" \
"${TOP_DIR}/tls-test -s --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/server/server.crt \
--tls_key_file ${CERT_DIR}/A/server/server.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all --allow_no_crl --once" \
"${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi


## 10-2 ##
run_test "10-2" \
"${TOP_DIR}/tls-test -s --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/server/server.crt \
--tls_key_file ${CERT_DIR}/A/server/server.key \
--tls_ca_certificate_path ${CERT_DIR}/B/cacerts_all --allow_no_crl --once" \
"${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/B/client/client.crt \
--tls_key_file ${CERT_DIR}/B/client/client.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi

## 10-3 ##
run_test "10-3" \
"${TOP_DIR}/tls-test -s --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/server/server.crt \
--tls_key_file ${CERT_DIR}/A/server/server.key \
--tls_ca_certificate_path ${CERT_DIR}/B/cacerts_all \
--tls_client_ca_certificate_path ${CERT_DIR}/B/cacerts_all \
--allow_no_crl --once" \
"${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/B/client/client.crt \
--tls_key_file ${CERT_DIR}/B/client/client.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all \
--allow_no_crl" ${debug_flag}
if [ $? -ne 0 ]; then
	fail_num=`expr ${fail_num} + 1`
fi


exit ${fail_num}
