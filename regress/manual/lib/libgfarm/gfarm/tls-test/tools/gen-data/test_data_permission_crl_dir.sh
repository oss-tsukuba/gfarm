#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
LIB_DIR=${TOP_DIR}/lib
OUTPUT_DIR=${PWD}/out
TEST_SUITE="permission_crl_dir"

## Funcs. ##

. ${LIB_DIR}/funcs.sh

usage() {
     cat << EOS >&2
Usage: `basename $0` [OPTION]...
Generate test data for ${TEST_SUITE}.

  OPTION:
    -o OUTPUT_DIR        Output dir. (default: ${OUTPUT_DIR})
    -h                   Help.
EOS
    exit 0
}

## Opts. ##
while getopts o:h OPT; do
    case ${OPT} in
        o) OUTPUT_DIR=${OPTARG};;
        h) usage;;
        *) usage;;
    esac
done
shift `expr $OPTIND - 1`

if [ ${EUID:-${UID}} != 0 -o -z "${SUDO_UID}" -o -z "${SUDO_GID}" ]; then
    echo "Need to sudo and run."
    exit 1
fi

# Generated certs.
gen_certs "${TEST_SUITE}" "${TOP_DIR}" "${OUTPUT_DIR}"
if [ $? -ne 0 ]; then
    puts_error "gen_certs"
    exit 1
fi

# set test data.
OUTPUT_DIR_TOP=${OUTPUT_DIR}
OUTPUT_DIR="${OUTPUT_DIR}/${TEST_SUITE}"

# Generated CRL.

TARGETS="A B"
for TARGET in ${TARGETS}; do
    ${TOP_DIR}/gen_crl.sh -r -I "${OUTPUT_DIR}/${TARGET}/cas/root_ca/" \
        -o "${OUTPUT_DIR}/${TARGET}/cas/root_ca/" -C "${OUTPUT_DIR}/${TARGET}/cacerts_all/root_ca.crt" \
        -R "ca.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh root"
        exit 1
    fi
done

mkdir -p "${OUTPUT_DIR}/A/crls/server/root"
mkdir -p "${OUTPUT_DIR}/B/crls/client/root/"
mkdir -p "${OUTPUT_DIR}/A/crls/server/root_bad_permissions"
mkdir -p "${OUTPUT_DIR}/B/crls/client/root_bad_permissions"

cp "${OUTPUT_DIR}/A/cas/root_ca/ca.crl" "${OUTPUT_DIR}/A/crls/server/root/root_ca.crl"
cp "${OUTPUT_DIR}/A/cas/root_ca/ca.crl" "${OUTPUT_DIR}/B/crls/client/root/root_ca.crl"
cp "${OUTPUT_DIR}/A/cas/root_ca/ca.crl" "${OUTPUT_DIR}/A/crls/server/root_bad_permissions/root_ca.crl"
cp "${OUTPUT_DIR}/A/cas/root_ca/ca.crl" "${OUTPUT_DIR}/B/crls/client/root_bad_permissions/root_ca.crl"

for TARGET in "${OUTPUT_DIR}/A/crls/server/root/" \
    "${OUTPUT_DIR}/B/crls/client/root/" \
    "${OUTPUT_DIR}/A/crls/server/root_bad_permissions/" \
    "${OUTPUT_DIR}/B/crls/client/root_bad_permissions/"; do
    ${TOP_DIR}/gen_hash_cert_files.sh "${TARGET}"
    if [ $? -ne 0 ]; then
        puts_error "gen_hash_cert_files.sh"
        exit 1
    fi
done

chmod 775 "${OUTPUT_DIR}/A/crls/server/root"
if [ $? -ne 0 ]; then
    puts_error "crls/server/root"
    exit 1
fi

chmod 775 "${OUTPUT_DIR}/B/crls/client/root/"
if [ $? -ne 0 ]; then
    puts_error "crls/client/root/"
    exit 1
fi

chmod 600 "${OUTPUT_DIR}/A/crls/server/root_bad_permissions"
if [ $? -ne 0 ]; then
    puts_error "crls/server/root_bad_permissions"
    exit 1
fi

chmod 600 "${OUTPUT_DIR}/B/crls/client/root_bad_permissions"
if [ $? -ne 0 ]; then
    puts_error "crls/client/root_bad_permissions"
    exit 1
fi

chown -R ${SUDO_UID}:${SUDO_GID} "${OUTPUT_DIR}"

chown root:root "${OUTPUT_DIR}/A/crls/server/root_bad_permissions"
if [ $? -ne 0 ]; then
    puts_error "crls/server/root_bad_permissions"
    exit 1
fi

chown root:root "${OUTPUT_DIR}/B/crls/client/root_bad_permissions"
if [ $? -ne 0 ]; then
    puts_error "crls/client/root_bad_permissions"
    exit 1
fi

exit 0
