#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd "${TOP_DIR}"; pwd`
TOP_DIR=`cd "${TOP_DIR}/../../"; pwd`
. "${TOP_DIR}/tools/testscripts/lib/funcs.sh"
ENV_DIR="${TOP_DIR}/test_dir"

expected_result_csv="${TOP_DIR}/tools/testscripts/expected-test-result.csv"
fail_num=0
CERT_DIR="${ENV_DIR}/permission_private_key"
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

## 8-1 ##
test_id="8-1"

${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all \
--allow_no_crl 2>/dev/null
client_exitstatus=$?

if [ ${debug_flag} -eq 1 ]; then 
        echo "client:${client_exitstatus}"
fi
expected_client_result=`cat ${expected_result_csv} | \
                grep -E "^${test_id}," | \
                awk -F "," '{print $3}' | sed 's:\r$::'`
if [ "${client_exitstatus}" = "${expected_client_result}" ]; then 
        echo "${test_id}:	PASS"
else
        echo "${test_id}:	FAIL"
	fail_num=`expr ${fail_num} + 1`
fi

## 8-2 ##
test_id="8-2"

${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client_bad_permissions.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all \
--allow_no_crl 2>/dev/null
client_exitstatus=$?

if [ ${debug_flag} -eq 1 ]; then 
        echo "client:${client_exitstatus}"
fi
expected_client_result=`cat ${expected_result_csv} | \
                grep -E "^${test_id}," | \
                awk -F "," '{print $3}' | sed 's:\r$::'`
if [ "${client_exitstatus}" = "${expected_client_result}" ]; then 
        echo "${test_id}:	PASS"
else
        echo "${test_id}:	FAIL"
	fail_num=`expr ${fail_num} + 1`
fi

## 8-3 ##
test_id="8-3"

${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client_bad_permissions_bad_user.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all \
--allow_no_crl 2>/dev/null
client_exitstatus=$?

if [ ${debug_flag} -eq 1 ]; then 
        echo "client:${client_exitstatus}"
fi
expected_client_result=`cat ${expected_result_csv} | \
                grep -E "^${test_id}," | \
                awk -F "," '{print $3}' | sed 's:\r$::'`
if [ "${client_exitstatus}" = "${expected_client_result}" ]; then 
        echo "${test_id}:	PASS"
else
        echo "${test_id}:	FAIL"
	fail_num=`expr ${fail_num} + 1`
fi

# 8-4 ##
test_id="8-4"

${TOP_DIR}/tls-test --mutual_authentication \
--tls_certificate_file ${CERT_DIR}/A/client/client.crt \
--tls_key_file ${CERT_DIR}/A/client/client_bad_permissions_bad_user2.key \
--tls_ca_certificate_path ${CERT_DIR}/A/cacerts_all \
--allow_no_crl 2>/dev/null
client_exitstatus=$?

if [ ${debug_flag} -eq 1 ]; then 
        echo "client:${client_exitstatus}"
fi
expected_client_result=`cat ${expected_result_csv} | \
                grep -E "^${test_id}," | \
                awk -F "," '{print $3}' | sed 's:\r$::'`
if [ "${client_exitstatus}" = "${expected_client_result}" ]; then 
        echo "${test_id}:	PASS"
else
        echo "${test_id}:	FAIL"
	fail_num=`expr ${fail_num} + 1`
fi

exit ${fail_num}
