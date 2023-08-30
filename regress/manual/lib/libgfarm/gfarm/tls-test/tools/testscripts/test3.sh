#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
TOP_DIR=`cd ${TOP_DIR}/../../; pwd`

. ${TOP_DIR}/tools/testscripts/lib/funcs.sh

_ret=1
expected_result_csv="${TOP_DIR}/tools/testscripts/expected-test-result.csv"
ENV_DIR="${TOP_DIR}/test_dir"

fail_flag=0
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

# 3-1
test_id="3-1"

echo "Input correct passphrase."
${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client_encrypted.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all 2>/dev/null
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
	fail_flag=1
fi

# 3-2
test_id="3-2"

echo "Input bad passphrase."
${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
--tls_key_file ${ENV_DIR}/A/client/client_encrypted.key \
--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all 2>/dev/null
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
	fail_flag=1
fi

# 3-3
run_test "3-3" \
	"${TOP_DIR}/tls-test -s --allow_no_crl --mutual_authentication \
	--tls_certificate_file ${ENV_DIR}/A/server/server.crt \
	--tls_key_file ${ENV_DIR}/A/server/server.key \
	--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all --once" \
	"${TOP_DIR}/tls-test --allow_no_crl --mutual_authentication \
	--tls_certificate_file ${ENV_DIR}/A/client/client.crt \
	--tls_key_file ${ENV_DIR}/A/client/client.key \
	--tls_ca_certificate_path ${ENV_DIR}/A/cacerts_all" ${debug_flag}

if [ $? -ne 0 ]; then
	fail_flag=1
fi

if [ ${fail_flag} -eq 0 ]; then
	_ret=0
fi

exit ${_ret}
