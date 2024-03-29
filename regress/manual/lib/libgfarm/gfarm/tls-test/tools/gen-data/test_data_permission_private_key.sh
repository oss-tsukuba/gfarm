#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
LIB_DIR=${TOP_DIR}/lib
OUTPUT_DIR=${PWD}/out
TEST_SUITE="permission_private_key"

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

cp "${OUTPUT_DIR}/A/client/client.key" "${OUTPUT_DIR}/A/client/client_bad_permissions.key"
cp "${OUTPUT_DIR}/A/client/client.key" "${OUTPUT_DIR}/A/client/client_bad_permissions_bad_user.key"
cp "${OUTPUT_DIR}/A/client/client.key" "${OUTPUT_DIR}/A/client/client_bad_permissions_bad_user2.key"

chmod 600 "${OUTPUT_DIR}/A/client/client.key"
if [ $? -ne 0 ]; then
    puts_error "client.key"
    exit 1
fi

chmod 666 "${OUTPUT_DIR}/A/client/client_bad_permissions.key"
if [ $? -ne 0 ]; then
    puts_error "client_bad_permissions.key"
    exit 1
fi

chmod 644 "${OUTPUT_DIR}/A/client/client_bad_permissions_bad_user.key"
if [ $? -ne 0 ]; then
    puts_error "client_bad_permissions_bad_user.key"
    exit 1
fi

chmod 640 "${OUTPUT_DIR}/A/client/client_bad_permissions_bad_user2.key"
if [ $? -ne 0 ]; then
    puts_error "client_bad_permissions_bad_user2.key"
    exit 1
fi

chown -R ${SUDO_UID}:${SUDO_GID} "${OUTPUT_DIR_TOP}"

chown root:root "${OUTPUT_DIR}/A/client/client_bad_permissions_bad_user.key"
if [ $? -ne 0 ]; then
    puts_error "client_bad_permissions_bad_user.key"
    exit 1
fi

chown root:root "${OUTPUT_DIR}/A/client/client_bad_permissions_bad_user2.key"
if [ $? -ne 0 ]; then
    puts_error "client_bad_permissions_bad_user2.key"
    exit 1
fi

exit 0
