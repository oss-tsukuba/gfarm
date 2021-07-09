#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`

OUTPUT_DIR=${PWD}/out
MAX_INTER_CA_NUM=10
INTER_CA_NUM=3
ISSUER_CA_SERVER_CERT=2 # inter_ca_2
ISSUER_CA_CLIENT_CERT=3 # inter_ca_3
IS_INTERACTIVE_INPUT_PASS="FALSE"
SUBJECT_SUFFIX="1"
PASS=test

PREFIX_INTER_CA_NAME="inter_ca_"
ROOT_CA_NAME="root_ca"
CLIENT_NAME="client"
SERVER_NAME="server"
SUBJECT_SERVER_CERT='/C=JP/ST=Tokyo/O=SRA/CN=gfarm_server${SUBJECT_SUFFIX}.sra.co.jp'
SUBJECT_CLIENT_CERT='/C=JP/ST=Tokyo/O=SRA/CN=gfarm_client${SUBJECT_SUFFIX}.sra.co.jp'


## Funcs. ##
usage() {
     cat << EOS >&2
Usage: `basename $0` [OPTION]...
Generate certificates & Concatenate certificates.

  OPTION:
    -o OUTPUT_DIR              Output dir. (default: ${OUTPUT_DIR})
    -n INTER_CA_NUM            Number of Intermediate CA.
                               (INTER_CA_NUM <= ${MAX_INTER_CA_NUM})
                               (default: ${INTER_CA_NUM})
    -s ISSUER_CA_SERVER_CERT   Issuer CA of Server certificate.
                               Specify suffix of Intermediate CA.
                               (default: ${ISSUER_CA_SERVER_CERT})
    -c ISSUER_CA_CLIENT_CERT   Issuer CA of Client certificate.
                               Specify suffix of Intermediate CA.
                               (default: ${ISSUER_CA_CLIENT_CERT})
    -S SUBJECT_SERVER_CERT     Subject of Server certificate.
                               Last subject must be CN.
                               (default: ${SUBJECT_SERVER_CERT})
    -C SUBJECT_CLIEN_CERT      Subject of Client certificate.
                               Last subject must be CN.
                               (default: ${SUBJECT_CLIENT_CERT})
    -X SUBJECT_SUFFIX          Suffix of subject. (default: ${SUBJECT_SUFFIX})
    -P                         Enable interactive password input.
                               Only the client private key password is valid.
                               If you do not specify this option, the password is "${PASS}".
                               (default: ${IS_INTERACTIVE_INPUT_PASS})
    -h                         Help.
EOS
    exit 0
}

puts_error() {
    echo "ERR: $1" >&2
}

gen_certs() {
    # gen Root cert.
    ${TOP_DIR}/gen_cert.sh -r -X ${SUBJECT_SUFFIX} -o ${OUTPUT_CAS_DIR} -R
    if [ $? -ne 0 ]; then
        puts_error "gen Root cert: $?"
        return 1
    fi
    for TARGET in ${OUTPUT_CACERTS_ALL_DIR}/${ROOT_CA_NAME}.crt \
        ${OUTPUT_CACERTS_ALL_CRL_DIR}/${ROOT_CA_NAME}.crt \
        ${OUTPUT_CACERTS_ROOT_DIR}/${ROOT_CA_NAME}.crt; do
        cp ${OUTPUT_CAS_DIR}/${ROOT_CA_NAME}/ca.crt ${TARGET}
    done
    cp ${OUTPUT_CAS_DIR}/${ROOT_CA_NAME}/ca.crl \
        ${OUTPUT_CACERTS_ALL_CRL_DIR}/${ROOT_CA_NAME}.crl

    # gen Intermediate cert.
    for CA_NUM in `seq 1 ${INTER_CA_NUM}`; do
        if [ ${CA_NUM} -eq 1 ]; then
            ISSUER=${OUTPUT_CAS_DIR}/${ROOT_CA_NAME}
        else
            ISSUER=${OUTPUT_CAS_DIR}/${PREFIX_INTER_CA_NAME}`expr ${CA_NUM} - 1`
        fi

        ${TOP_DIR}/gen_cert.sh -i -I ${ISSUER} -x ${CA_NUM} -X ${SUBJECT_SUFFIX} \
            -o ${OUTPUT_CAS_DIR} -R
        if [ $? -ne 0 ]; then
            puts_error "gen Intermediate cert ${CA_NUM}: $?"
            return 1
        fi
        cp ${OUTPUT_CAS_DIR}/${PREFIX_INTER_CA_NAME}${CA_NUM}/ca.crt \
            ${OUTPUT_CACERTS_ALL_DIR}/${PREFIX_INTER_CA_NAME}${CA_NUM}.crt
        cp ${OUTPUT_CAS_DIR}/${PREFIX_INTER_CA_NAME}${CA_NUM}/ca.crt \
            ${OUTPUT_CACERTS_ALL_CRL_DIR}/${PREFIX_INTER_CA_NAME}${CA_NUM}.crt
        cp ${OUTPUT_CAS_DIR}/${PREFIX_INTER_CA_NAME}${CA_NUM}/ca.crl \
            ${OUTPUT_CACERTS_ALL_CRL_DIR}/${PREFIX_INTER_CA_NAME}${CA_NUM}.crl
    done

    # gen Server cert.
    ${TOP_DIR}/gen_cert.sh -s \
        -I ${OUTPUT_CAS_DIR}/${PREFIX_INTER_CA_NAME}${ISSUER_CA_SERVER_CERT} \
        -o ${OUTPUT_CAS_DIR} -S "${SUBJECT_SERVER_CERT}"
    if [ $? -ne 0 ]; then
        puts_error "gen Server cert: $?"
        return 1
    fi
    cp ${OUTPUT_CAS_DIR}/${SERVER_NAME}/${SERVER_NAME}.crt ${OUTPUT_SERVER_DIR}/
    cp ${OUTPUT_CAS_DIR}/${SERVER_NAME}/${SERVER_NAME}.key ${OUTPUT_SERVER_DIR}/

    # gen Client cert.
    OPTS=""
    ${TOP_DIR}/gen_cert.sh -c \
        -I ${OUTPUT_CAS_DIR}/${PREFIX_INTER_CA_NAME}${ISSUER_CA_CLIENT_CERT} \
        -o ${OUTPUT_CAS_DIR} -S "${SUBJECT_CLIENT_CERT}" -p ${PASS}
    if [ $? -ne 0 ]; then
        puts_error "gen Client cert: $?"
        return 1
    fi
    cp ${OUTPUT_CAS_DIR}/${CLIENT_NAME}/${CLIENT_NAME}.crt ${OUTPUT_CLIENT_DIR}/
    cp ${OUTPUT_CAS_DIR}/${CLIENT_NAME}/${CLIENT_NAME}.key ${OUTPUT_CLIENT_DIR}/
    cp ${OUTPUT_CAS_DIR}/${CLIENT_NAME}/${CLIENT_NAME}_encrypted.key ${OUTPUT_CLIENT_DIR}/
}

gen_hash_certs() {
    for TARGET in ${OUTPUT_CACERTS_ALL_DIR} ${OUTPUT_CACERTS_ALL_CRL_DIR} \
        ${OUTPUT_CACERTS_ROOT_DIR}; do
        ${TOP_DIR}/gen_hash_cert_files.sh ${TARGET}
        if [ $? -ne 0 ]; then
            puts_error "cat certs: $?"
            return 1
        fi
    done
}

cat_certs() {
    ISSUER_CA=$1
    CAT_CERT="$2"
    OUT_CAT_CERT=$3

    for CA_NUM in `seq ${ISSUER_CA} -1 1`; do
        CAT_CERT+=" ${OUTPUT_CACERTS_ALL_DIR}/${PREFIX_INTER_CA_NAME}${CA_NUM}.crt"
    done
    CAT_CERT+=" ${OUTPUT_CACERTS_ALL_DIR}/${ROOT_CA_NAME}.crt"

    cat ${CAT_CERT} > ${OUT_CAT_CERT}
}

cat_certs_without_end_entity() {
    ISSUER_CA=$1
    OUT_CAT_CERT=$2
    CAT_CERT=""

    for CA_NUM in `seq ${ISSUER_CA} -1 1`; do
        CAT_CERT+=" ${OUTPUT_CACERTS_ALL_DIR}/${PREFIX_INTER_CA_NAME}${CA_NUM}.crt"
    done
    CAT_CERT+=" ${OUTPUT_CACERTS_ALL_DIR}/${ROOT_CA_NAME}.crt"

    cat ${CAT_CERT} > ${OUT_CAT_CERT}
}


## Opts. ##
while getopts o:n:s:c:S:C:X:Ph OPT; do
    case ${OPT} in
        o) OUTPUT_DIR=${OPTARG};;
        n) INTER_CA_NUM=${OPTARG};;
        s) ISSUER_CA_SERVER_CERT=${OPTARG};;
        c) ISSUER_CA_CLIENT_CERT=${OPTARG};;
        S) SUBJECT_SERVER_CERT=${OPTARG};;
        C) SUBJECT_CLIENT_CERT=${OPTARG};;
        X) SUBJECT_SUFFIX=${OPTARG};;
        P) IS_INTERACTIVE_INPUT_PASS="TRUE";;
        h) usage;;
        *) usage;;
    esac
done
shift `expr $OPTIND - 1`


## Main. ##
if [ -d ${OUTPUT_DIR} ]; then
    puts_error "already exists: ${OUTPUT_DIR}"
    exit 1
fi

if [ ${INTER_CA_NUM} -gt ${MAX_INTER_CA_NUM} ]; then
    puts_error "-n INTER_CA_NUM, ${INTER_CA_NUM} <= ${MAX_INTER_CA_NUM}"
    exit 1
fi

# input passwd.
if [ "x${IS_INTERACTIVE_INPUT_PASS}" = "xTRUE" ] ; then
    read -sp "Password of client private key: " PASS
fi

# Lazy evaluation.
SUBJECT_SERVER_CERT=`eval echo ${SUBJECT_SERVER_CERT}`
SUBJECT_CLIENT_CERT=`eval echo ${SUBJECT_CLIENT_CERT}`

OUTPUT_CAS_DIR=${OUTPUT_DIR}/cas
OUTPUT_SERVER_DIR=${OUTPUT_DIR}/server
OUTPUT_CLIENT_DIR=${OUTPUT_DIR}/client
OUTPUT_CACERTS_ALL_DIR=${OUTPUT_DIR}/cacerts_all
OUTPUT_CACERTS_ALL_CRL_DIR=${OUTPUT_DIR}/cacerts_all_crl
OUTPUT_CACERTS_ROOT_DIR=${OUTPUT_DIR}/cacerts_root

rm -rf ${OUTPUT_DIR}
mkdir -p ${OUTPUT_CAS_DIR}
mkdir -p ${OUTPUT_SERVER_DIR}
mkdir -p ${OUTPUT_CLIENT_DIR}
mkdir -p ${OUTPUT_CACERTS_ALL_DIR}
mkdir -p ${OUTPUT_CACERTS_ALL_CRL_DIR}
mkdir -p ${OUTPUT_CACERTS_ROOT_DIR}

# gen certs.
gen_certs
if [ $? -ne 0 ]; then
    exit 1
fi

# gen hash certs.
gen_hash_certs
if [ $? -ne 0 ]; then
    exit 1
fi

# cat server cert.
cat_certs "${ISSUER_CA_SERVER_CERT}" \
    "${OUTPUT_SERVER_DIR}/${SERVER_NAME}.crt" \
    "${OUTPUT_SERVER_DIR}/${SERVER_NAME}_cat_all.crt"
if [ $? -ne 0 ]; then
    exit 1
fi

# cat server cert (without_end_entity).
cat_certs_without_end_entity "${ISSUER_CA_SERVER_CERT}" \
    "${OUTPUT_SERVER_DIR}/${SERVER_NAME}_cat_all.crt"
if [ $? -ne 0 ]; then
    exit 1
fi

# cat client cert.
cat_certs "${ISSUER_CA_CLIENT_CERT}" \
    "${OUTPUT_CLIENT_DIR}/${CLIENT_NAME}.crt" \
    "${OUTPUT_CLIENT_DIR}/${CLIENT_NAME}_cat_all.crt"
if [ $? -ne 0 ]; then
    exit 1
fi

# cat client cert (without_end_entity).
cat_certs_without_end_entity "${ISSUER_CA_CLIENT_CERT}" \
    "${OUTPUT_CLIENT_DIR}/${CLIENT_NAME}_cat_all_without_end_entity.crt"
if [ $? -ne 0 ]; then
    exit 1
fi
