#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
LIB_DIR=${TOP_DIR}/lib
OUTPUT_DIR=${PWD}/out
TEST_SUITE="verify_chain_path"

PASS=test
CLIENT_NAME="client_under_inter_ca_4"
SERVER_NAME="server_under_inter_ca_4"
PREFIX_INTER_CA_NAME="inter_ca_"

ISSUER_CA_SERVER_CERT=4
ISSUER_CA_CLIENT_CERT=4
CA_NUM=4

SUBJECT_SERVER_CERT_TPL='/C=JP/ST=Tokyo/O=SRA/CN=gfarm_server${SUBJECT_SUFFIX}.sra.co.jp'
SUBJECT_CLIENT_CERT_TPL='/C=JP/ST=Tokyo/O=SRA/CN=gfarm_client${SUBJECT_SUFFIX}.sra.co.jp'
SUBJECT_SUFFIX_BASE=4

## Funcs. ##

source ${LIB_DIR}/funcs.sh

usage() {
     cat << EOS >&2
Usage: `basename $0` [OPTION]...
Generate test data for ${TEST_SUITE}.

  OPTION:
    -o OUTPUT_DIR                Output dir. (default: ${OUTPUT_DIR})
    -h                           Help.
EOS
    exit 0
}

gen_inter() {
    issuer=$1
    subject_suffix=$2
    output_cas_dir=$3

    ${TOP_DIR}/gen_cert.sh -i -I ${issuer} -x ${CA_NUM} -X ${subject_suffix} \
        -o ${output_cas_dir} -R
    if [ $? -ne 0 ]; then
        puts_error "gen Intermediate cert ${CA_NUM}: $?"
        return 1
    fi
}

gen_server() {
    output_cas_dir=$1
    output_dir=$2

    ${TOP_DIR}/gen_cert.sh -s \
        -I ${output_cas_dir}/${PREFIX_INTER_CA_NAME}${ISSUER_CA_SERVER_CERT} \
        -o ${output_cas_dir} -S "${SUBJECT_SERVER_CERT}" -D ${SERVER_NAME}
    if [ $? -ne 0 ]; then
        puts_error "gen Server cert: $?"
        return 1
    fi

    mkdir -p ${output_dir}/
    cp ${output_cas_dir}/${SERVER_NAME}/server.crt ${output_dir}/
    cp ${output_cas_dir}/${SERVER_NAME}/server.key ${output_dir}/
}

gen_client() {
    output_cas_dir=$1
    output_dir=$2

    ${TOP_DIR}/gen_cert.sh -c \
        -I ${output_cas_dir}/${PREFIX_INTER_CA_NAME}${ISSUER_CA_CLIENT_CERT} \
        -o ${output_cas_dir} -S "${SUBJECT_CLIENT_CERT}" -p ${PASS} -D ${CLIENT_NAME}
    if [ $? -ne 0 ]; then
        puts_error "gen Client cert: $?"
        return 1
    fi

    mkdir -p ${output_dir}/
    cp ${output_cas_dir}/${CLIENT_NAME}/client.crt ${output_dir}/
    cp ${output_cas_dir}/${CLIENT_NAME}/client.key ${output_dir}/
}

gen_cacerts_all_for_AB() {
    output_certs_all_dir=$1
    output_certs_all2_dir=$2

    cp ${output_certs_all_dir}/*\.crt ${output_certs_all2_dir}/
    ${TOP_DIR}/gen_hash_cert_files.sh "${output_certs_all2_dir}"
    if [ $? -ne 0 ]; then
        puts_error "gen_hash_cert_files.sh"
        exit 1
    fi
}

## Opts. ##
while getopts o:h OPT; do
    case ${OPT} in
        o) OUTPUT_DIR=`cd ${OPTARG}; pwd`;;
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

# Generated certs chain.
gen_certs_chain "${TEST_SUITE}" "${TOP_DIR}" "${OUTPUT_DIR}"
if [ $? -ne 0 ]; then
    puts_error "gen_certs_chain"
    exit 1
fi

# set test data.
OUTPUT_SUITE_DIR="${OUTPUT_DIR}/${TEST_SUITE}"

OUTPUT_AB_CERTS_ALL_DIR="${OUTPUT_SUITE_DIR}/A_B/cacerts_all"
OUTPUT_AB_CERTS_ALL2_DIR="${OUTPUT_SUITE_DIR}/A_B/cacerts_all_under_inter_ca_4"

mkdir -p ${OUTPUT_AB_CERTS_ALL2_DIR}

SEQ=0
TARGETS="A B"
for TARGET in ${TARGETS};do
    SUFFIX=`expr ${SEQ} + 1`
    SUBJECT_SUFFIX=`expr ${SEQ} + ${SUBJECT_SUFFIX_BASE}`

    OUTPUT_CAS_DIR="${OUTPUT_SUITE_DIR}/${TARGET}/cas"
    OUTPUT_CLIENT_DIR="${OUTPUT_SUITE_DIR}/${TARGET}/${CLIENT_NAME}"
    OUTPUT_SERVER_DIR="${OUTPUT_SUITE_DIR}/${TARGET}/${SERVER_NAME}"
    OUTPUT_CERTS_ALL_DIR="${OUTPUT_SUITE_DIR}/${TARGET}/cacerts_all"
    OUTPUT_CERTS_ALL2_DIR="${OUTPUT_SUITE_DIR}/${TARGET}/cacerts_all_under_inter_ca_4"

    # Lazy evaluation.
    SUBJECT_SERVER_CERT=`eval echo ${SUBJECT_SERVER_CERT_TPL}`
    SUBJECT_CLIENT_CERT=`eval echo ${SUBJECT_CLIENT_CERT_TPL}`

    # generate intermediate CA-4.
    gen_inter "${OUTPUT_SUITE_DIR}/${TARGET}/cas/inter_ca_3" "${SUFFIX}" "${OUTPUT_CAS_DIR}"
    if [ $? -ne 0 ]; then
        puts_error "gen_inter"
        exit 1
    fi

    # generate server cert under the intermediate CA-4.
    gen_server "${OUTPUT_CAS_DIR}" "${OUTPUT_SERVER_DIR}"
    if [ $? -ne 0 ]; then
        puts_error "gen_server"
        exit 1
    fi

    # generate client cert under the intermediate CA-4.
    gen_client "${OUTPUT_CAS_DIR}" "${OUTPUT_CLIENT_DIR}"
    if [ $? -ne 0 ]; then
        puts_error "gen_client"
        exit 1
    fi

    cp ${OUTPUT_SUITE_DIR}/${TARGET}/cas/${PREFIX_INTER_CA_NAME}${ISSUER_CA_CLIENT_CERT}/ca.crt \
        ${OUTPUT_AB_CERTS_ALL2_DIR}/${PREFIX_INTER_CA_NAME}${TARGET}_${ISSUER_CA_CLIENT_CERT}.crt

    SEQ=`expr ${SEQ} + 1`
done

# generate cacerts_all dir for A_B.
gen_cacerts_all_for_AB "${OUTPUT_AB_CERTS_ALL_DIR}" "${OUTPUT_AB_CERTS_ALL2_DIR}"
if [ $? -ne 0 ]; then
    puts_error "gen_cacerts_all_for_AB: $?"
    exit 1
fi

exit 0
