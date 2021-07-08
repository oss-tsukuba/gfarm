#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
CONF_DIR=${TOP_DIR}/conf
LIB_DIR=${TOP_DIR}/lib
OUTPUT_DIR=${PWD}/out
TEST_SUITE="proxy_cert"

SUBJECT_CLIENT_CERT='/C=JP/ST=Tokyo/O=SRA/CN=user'
SUBJECT_CLIENT_PROXY_CERT='/C=JP/ST=Tokyo/O=SRA/CN=user/CN=1234'
SUBJECT_SERVER_CERT='/C=JP/ST=Tokyo/O=SRA/CN=gfarm_server_proxy_cert1.sra.co.jp'

PASS=test
ROOT_CA_NAME="root_ca"
CLIENT_NAME="client"
CLIENT_PROXY_NAME="client_proxy"
CLIENT_CAT_NAME="client_cat_all"
SERVER_NAME="server"

## Funcs. ##

source ${LIB_DIR}/funcs.sh

usage() {
     cat << EOS >&2
Usage: `basename $0` [OPTION]...
Generate test data for ${TEST_SUITE}.

  OPTION:
    -o OUTPUT_DIR                Output dir. (default: ${OUTPUT_DIR})
    -S SUBJECT_SERVER_CERT       Subject of Server certificate.
                                 Last subject must be CN.
                                 (default: ${SUBJECT_SERVER_CERT}
    -C SUBJECT_CLIEN_CERT        Subject of Client certificate.
                                 Last subject must be CN.
                                 (default: ${SUBJECT_CLIENT_CERT}
    -P SUBJECT_CLIEN_PROXY_CERT  Subject of Client certificate for proxy certificate.
                                 Last subject must be CN.
                                 (default: ${SUBJECT_CLIENT_PROXY_CERT})
    -h                           Help.
EOS
    exit 0
}

gen_certs_under_root() {
    # gen Server cert under the Root cert.
    ${TOP_DIR}/gen_cert.sh -s \
        -I ${ISSUER} \
        -o ${OUTPUT_CAS_DIR} -S "${SUBJECT_SERVER_CERT}"
    if [ $? -ne 0 ]; then
        puts_error "gen Server cert: $?"
        exit 1
    fi
    cp ${OUTPUT_CAS_DIR}/${SERVER_NAME}/${SERVER_NAME}.crt ${OUTPUT_SERVER_DIR}/
    cp ${OUTPUT_CAS_DIR}/${SERVER_NAME}/${SERVER_NAME}.key ${OUTPUT_SERVER_DIR}

    # gen Client cert under the Root cert.
    ${TOP_DIR}/gen_cert.sh -c \
        -I ${ISSUER} \
        -o ${OUTPUT_CAS_DIR} -S "${SUBJECT_CLIENT_CERT}" -p ${PASS}
    if [ $? -ne 0 ]; then
        puts_error "gen Client cert: $?"
        exit 1
    fi

    cp ${OUTPUT_CAS_DIR}/${CLIENT_NAME}/${CLIENT_NAME}.crt ${OUTPUT_CLIENT_DIR}/
    cp ${OUTPUT_CAS_DIR}/${CLIENT_NAME}/${CLIENT_NAME}.key ${OUTPUT_CLIENT_DIR}/
    cp ${OUTPUT_CAS_DIR}/${CLIENT_NAME}/${CLIENT_NAME}_encrypted.key ${OUTPUT_CLIENT_DIR}/
}

gen_proxy_certs() {
    openssl req -passout pass:${PASS} -new -config ${CONF} \
        -out ${CLIENT_PROXY_NAME}.req \
        -keyout ${CLIENT_PROXY_NAME}.key \
        -subj "${SUBJECT_CLIENT_PROXY_CERT}"
    if [ $? -ne 0 ]; then
        puts_error "openssl req for proxy: $?"
        exit 1
    fi

    openssl rsa -passin pass:${PASS} -in ${CLIENT_PROXY_NAME}.key \
        -out ${CLIENT_PROXY_NAME}.key
    if [ $? -ne 0 ]; then
        puts_error "openssl rsa for proxy: $?"
        exit 1
    fi

    openssl x509 -req -CAcreateserial -in ${CLIENT_PROXY_NAME}.req \
        -out ${CLIENT_PROXY_NAME}.crt \
        -CA ${CLIENT_NAME}.crt \
        -CAkey ${CLIENT_NAME}.key \
        -days 7 -extfile ${CONF} -extensions v3_proxy
    if [ $? -ne 0 ]; then
        puts_error "openssl req x509 for proxy: $?"
        exit 1
    fi

    cp ${OUTPUT_CAS_DIR}/${CLIENT_NAME}/${CLIENT_PROXY_NAME}.crt ${OUTPUT_CLIENT_DIR}/
    sed -n -e '/\-\-\-\-\-BEGIN/,/-\-\-\-\-END/p' ${OUTPUT_CLIENT_DIR}\/${CLIENT_NAME}.crt > \
        ${OUTPUT_CLIENT_DIR}/${CLIENT_NAME}_no_text.crt
    cp ${OUTPUT_CAS_DIR}/${CLIENT_NAME}/${CLIENT_PROXY_NAME}.key ${OUTPUT_CLIENT_DIR}/
}

cat_certs() {
    cat ${OUTPUT_CLIENT_DIR}/${CLIENT_PROXY_NAME}.crt \
        ${OUTPUT_CLIENT_DIR}/${CLIENT_PROXY_NAME}.key \
        ${OUTPUT_CLIENT_DIR}/${CLIENT_NAME}_no_text.crt > \
        ${OUTPUT_CLIENT_DIR}/${CLIENT_CAT_NAME}.crt
    chmod 0600 ${OUTPUT_CLIENT_DIR}/${CLIENT_CAT_NAME}.crt
    if [ $? -ne 0 ]; then
        puts_error "chmod: cat certs: $?"
        exit 1
    fi
    cp ${OUTPUT_CLIENT_DIR}/${CLIENT_CAT_NAME}.crt \
        ${OUTPUT_CLIENT_DIR}/${CLIENT_CAT_NAME}_bad_permissions.crt
    chmod 0644 ${OUTPUT_CLIENT_DIR}/${CLIENT_CAT_NAME}_bad_permissions.crt
    if [ $? -ne 0 ]; then
        puts_error "chmod: cat certs: $?"
        exit 1
    fi
}

## Opts. ##
while getopts o:S:C:P:h OPT; do
    case ${OPT} in
        o) OUTPUT_DIR=`cd ${OPTARG}; pwd`;;
        S) SUBJECT_SERVER_CERT=${OPTARG};;
        C) SUBJECT_CLIENT_CERT=${OPTARG};;
        P) SUBJECT_CLIENT_PROXY_CERT=${OPTARG};;
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
OUTPUT_SUITE_DIR="${OUTPUT_DIR}/${TEST_SUITE}"
OUTPUT_CAS_DIR="${OUTPUT_SUITE_DIR}/A/cas"
OUTPUT_CLIENT_DIR="${OUTPUT_SUITE_DIR}/A/client_under_root"
OUTPUT_SERVER_DIR="${OUTPUT_SUITE_DIR}/A/server_under_root"

ISSUER=${OUTPUT_CAS_DIR}/${ROOT_CA_NAME}
CONF=${CONF_DIR}/openssl_client_proxy_cert.cnf

mkdir -p ${OUTPUT_CLIENT_DIR}
mkdir -p ${OUTPUT_SERVER_DIR}

# generate certificates.
gen_certs_under_root
pushd ${OUTPUT_CAS_DIR}/${CLIENT_NAME}
gen_proxy_certs
cat_certs
popd

exit 0
