#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`

source ${TOP_DIR}/tools/testscripts/lib/funcs.sh

_ret=1
fail_flag=0
ENV_DIR="${TOP_DIR}/test_dir"
debug_flag=0

usage(){
	cat << EOS >&2
Usage:

	OPTION:
		-d			Debug flag
		-h			Help
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
# 12-1
run_test "12-1" \
"${TOP_DIR}/tls-test -s --allow_no_crl \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${ENV_DIR}/A/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--tls_cipher_suite \
TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256:\
TLS_AES_128_CCM_SHA256:TLS_AES_128_CCM_8_SHA256 \
--once" \
"${TOP_DIR}/tls-test --allow_no_crl \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--tls_cipher_suite \
TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256:\
TLS_AES_128_CCM_SHA256:TLS_AES_128_CCM_8_SHA256" ${debug_flag}

if [ $? -ne 0 ]; then
	fail_flag=1
fi

# 12-2
run_test "12-2" \
"${TOP_DIR}/tls-test -s --allow_no_crl \
--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
--tls_key_file ${ENV_DIR}/A/server/server.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--tls_cipher_suite \
TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256:\
TLS_AES_128_CCM_SHA256:TLS_AES_128_CCM_8_SHA256 \
--once" \
"${TOP_DIR}/tls-test --allow_no_crl \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all \
--tls_cipher_suite TLS_AES_256_GCM_SHA384" ${debug_flag}

if [ $? -ne 0 ]; then
	fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then
	_ret=0
fi

exit ${_ret}
