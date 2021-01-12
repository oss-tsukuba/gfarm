#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
LIB_DIR=${TOP_DIR}/lib
OUTPUT_DIR=${PWD}/out
TEST_SUITE="permission_crl_dir"

## Funcs. ##

source ${LIB_DIR}/funcs.sh

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

# Generated certs.
gen_certs "${TEST_SUITE}" "${TOP_DIR}" "${OUTPUT_DIR}"
if [ $? -ne 0 ]; then
    puts_error "gen_certs"
    exit 1
fi

# set test data.
OUTPUT_DIR="${OUTPUT_DIR}/${TEST_SUITE}"

cp -r "${OUTPUT_DIR}/A/crls/server/root/" "${OUTPUT_DIR}/A/crls/server/root_bad_permissions"
cp -r "${OUTPUT_DIR}/A/crls/client/root/" "${OUTPUT_DIR}/A/crls/client/root_bad_permissions"

chmod 775 "${OUTPUT_DIR}/A/crls/server/root"
if [ $? -ne 0 ]; then
    puts_error "crls/server/root"
    exit 1
fi

chmod 775 "${OUTPUT_DIR}/A/crls/client/root/"
if [ $? -ne 0 ]; then
    puts_error "crls/client/root/"
    exit 1
fi

chmod 600 "${OUTPUT_DIR}/A/crls/server/root_bad_permissions"
if [ $? -ne 0 ]; then
    puts_error "crls/server/root_bad_permissions"
    exit 1
fi

chmod 600 "${OUTPUT_DIR}/A/crls/client/root_bad_permissions"
if [ $? -ne 0 ]; then
    puts_error "crls/client/root_bad_permissions"
    exit 1
fi

sudo chown root:root "${OUTPUT_DIR}/A/crls/server/root_bad_permissions"
if [ $? -ne 0 ]; then
    puts_error "A/crls/server/root_bad_permissions"
    exit 1
fi

sudo chown root:root "${OUTPUT_DIR}/A/crls/client/root_bad_permissions"
if [ $? -ne 0 ]; then
    puts_error "A/crls/client/root_bad_permissions"
    exit 1
fi

exit 0
