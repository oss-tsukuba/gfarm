#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
LIB_DIR=${TOP_DIR}/lib
OUTPUT_DIR=${PWD}/out
TEST_SUITE="server_client"

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

# Generated certs.
gen_certs "${TEST_SUITE}" "${TOP_DIR}" "${OUTPUT_DIR}"
if [ $? -ne 0 ]; then
    puts_error "gen_certs"
    exit 1
fi

# set test data.
OUTPUT_DIR="${OUTPUT_DIR}/${TEST_SUITE}"

cat "${OUTPUT_DIR}/test/A/cas/server/server.crt" "test/A/cas/inter_ca_1/ca.crt" > \
    "${OUTPUT_DIR}/test/A/server/server_cat_1.crt"
if [ $? -ne 0 ]; then
    puts_error "server_cat_1.crt"
    exit 1
fi

cat "${OUTPUT_DIR}/test/A/cas/client/client.crt" "test/A/cas/inter_ca_1/ca.crt" > \
    "${OUTPUT_DIR}/test/A/client/client_cat_1.crt"
if [ $? -ne 0 ]; then
    puts_error "client_cat_1.crt"
    exit 1
fi

exit 0
