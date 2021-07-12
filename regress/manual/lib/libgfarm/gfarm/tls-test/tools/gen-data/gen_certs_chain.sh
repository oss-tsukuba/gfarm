#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`

usage() {
    cat << EOS
Generate test data for certs-chain file

  OPTION:
    -d CERT_DIR       Must specify cert_directory.
    -h                Help.
EOS
    exit 0
}

while getopts d:h OPT; do
    case ${OPT} in
        d) CERT_DIR=${OPTARG};;
        h) usage;;
        *) usage;;
    esac
done
shift `expr $OPTIND - 1`

## client cert chain ##
make_client_chain() {
    for i in ${CERT_A_DIR} ${CERT_B_DIR}; do
        ret=1
        cat ${i}/client/client.crt \
        ${i}/cacerts_all/inter_ca_3.crt \
        ${i}/cacerts_all/inter_ca_2.crt \
        ${i}/cacerts_all/inter_ca_1.crt \
        > ${i}/client/client_cat_3_2_1.crt \
        && cat ${i}/client/client.crt \
        ${i}/cacerts_all/inter_ca_3.crt \
        ${i}/cacerts_all/inter_ca_2.crt \
        > ${i}/client/client_cat_3_2.crt \
        && cat ${i}/client/client.crt \
        ${i}/cacerts_all/inter_ca_3.crt \
        > ${i}/client/client_cat_3.crt \
        && cat ${i}/client/client.crt \
        ${i}/cacerts_all/inter_ca_3.crt \
        ${i}/cacerts_all/inter_ca_1.crt \
        > ${i}/client/client_cat_3_1.crt \
        && cat ${i}/cacerts_all/inter_ca_3.crt \
        ${i}/cacerts_all/inter_ca_2.crt \
        ${i}/cacerts_all/inter_ca_1.crt \
        ${i}/cacerts_all/root_ca.crt \
        > ${i}/client/client_cat_all_without_end_entity.crt \
        && cat ${i}/cacerts_all/inter_ca_3.crt \
        ${i}/cacerts_all/inter_ca_1.crt \
        ${i}/cacerts_all/root_ca.crt \
        > ${i}/client/client_cat_all_without_end_entity_inter_ca_2.crt \
        && cat ${i}/cacerts_all/inter_ca_2.crt \
        ${i}/cacerts_all/inter_ca_1.crt \
        ${i}/cacerts_all/root_ca.crt \
        ${i}/cacerts_all/inter_ca_3.crt \
        > ${i}/client/client_cat_all_without_end_entity_disorder.crt
        if [ $? -ne 0 ]; then
            break
        fi
        ret=0
    done
    return $ret
}

## server cert chain ##
make_server_chain() {
    for i in ${CERT_A_DIR} ${CERT_B_DIR}; do
        ret=1
        cat ${i}/server/server.crt \
        ${i}/cacerts_all/inter_ca_2.crt \
        ${i}/cacerts_all/inter_ca_1.crt \
        > ${i}/server/server_cat_2_1.crt \
        && cat ${i}/server/server.crt \
        ${i}/cacerts_all/inter_ca_2.crt \
        > ${i}/server/server_cat_2.crt \
        && cat ${i}/server/server.crt \
        ${i}/cacerts_all/inter_ca_1.crt \
        > ${i}/server/server_cat_1.crt \
        && cat ${i}/cacerts_all/inter_ca_2.crt \
        ${i}/cacerts_all/inter_ca_1.crt \
        ${i}/cacerts_all/root_ca.crt \
        > ${i}/server/server_cat_all_without_end_entity.crt \
        && cat ${i}/cacerts_all/inter_ca_1.crt \
        ${i}/cacerts_all/root_ca.crt \
        > ${i}/server/server_cat_all_without_end_entity_inter_ca_2.crt \
        && cat ${i}/cacerts_all/inter_ca_1.crt \
        ${i}/cacerts_all/root_ca.crt \
        ${i}/cacerts_all/inter_ca_2.crt \
        > ${i}/server/server_cat_all_without_end_entity_disorder.crt
        if [ $? -ne 0 ]; then
            break
        fi
        ret=0
    done
    return $ret
}

## mk cert dir ##
make_cert_dir() {
    for i in ${CERT_A_DIR} ${CERT_B_DIR}; do
        ret=1
        mkdir -p ${i}/cacerts_root_1_2 && mkdir -p ${i}/cacerts_root_1 \
        && mkdir -p ${i}/cacerts_root_2
        if [ $? -ne 0 ]; then
            break
        fi

        tar -C ${i}/cacerts_all/ -cpf - . | \
        tar xvpf - -C ${i}/cacerts_root_1_2 \
        && ca_rm ${i}/cacerts_root_1_2 ${i}/cacerts_root_1_2/inter_ca_3.crt
        if [ $? -ne 0 ]; then
            break
        fi

        tar -C ${i}/cacerts_root_1_2/ -cpf - . | \
        tar xvpf - -C ${i}/cacerts_root_1 \
        && ca_rm ${i}/cacerts_root_1 ${i}/cacerts_root_1/inter_ca_2.crt
        if [ $? -ne 0 ]; then
            break
        fi

        tar -C ${i}/cacerts_root_1_2/ -cpf - . | \
        tar xvpf - -C ${i}/cacerts_root_2 \
        && ca_rm ${i}/cacerts_root_2 ${i}/cacerts_root_2/inter_ca_1.crt
        if [ $? -ne 0 ]; then
            break
        fi
        ret=0
    done
    return $ret
}

## ca_rm ##
ca_rm() {
    if [ $# != 2 -o "$1" = "" -o "$2" = "" ]; then
        return 1
    fi
    hash=`openssl x509 -hash -noout -in "$2"`
    /bin/rm -f "$1/${hash}.0" && /bin/rm -f "$2"
}

## A_B certs all ##
make_ab() {
    ret=1
    mkdir -p ${CERT_DIR}/A_B/cacerts_all
    if [ $? -ne 0 ]; then
        return $ret
    fi

    cp ${CACERTS_ALL_A_DIR}/root_ca.crt \
    ${CERT_DIR}/A_B/cacerts_all/root_ca_A.crt \
    && cp ${CACERTS_ALL_A_DIR}/inter_ca_1.crt \
    ${CERT_DIR}/A_B/cacerts_all/inter_ca_A_1.crt \
    && cp ${CACERTS_ALL_A_DIR}/inter_ca_2.crt \
    ${CERT_DIR}/A_B/cacerts_all/inter_ca_A_2.crt \
    && cp ${CACERTS_ALL_A_DIR}/inter_ca_3.crt \
    ${CERT_DIR}/A_B/cacerts_all/inter_ca_A_3.crt \
    && cp ${CACERTS_ALL_B_DIR}/root_ca.crt \
    ${CERT_DIR}/A_B/cacerts_all/root_ca_B.crt \
    && cp ${CACERTS_ALL_B_DIR}/inter_ca_1.crt \
    ${CERT_DIR}/A_B/cacerts_all/inter_ca_B_1.crt \
    && cp ${CACERTS_ALL_B_DIR}/inter_ca_2.crt \
    ${CERT_DIR}/A_B/cacerts_all/inter_ca_B_2.crt \
    && cp ${CACERTS_ALL_B_DIR}/inter_ca_3.crt \
    ${CERT_DIR}/A_B/cacerts_all/inter_ca_B_3.crt
    if [ $? -ne 0 ]; then
        return $ret
    fi

    ${TOP_DIR}/gen_hash_cert_files.sh ${CERT_DIR}/A_B/cacerts_all/
    if [ $? -ne 0 ]; then
        return $ret
    fi
    ret=0
    return $ret
}

### main ###
if [ ! -d "${CERT_DIR}/A" -a -d "${CERT_DIR}/B" ]; then
    echo "A or B dir not exists" >&2
    exit 1
fi
CERT_A_DIR=${CERT_DIR}/A
CERT_B_DIR=${CERT_DIR}/B

if [ ! -d "${CERT_A_DIR}/cacerts_all" -a -d "${CERT_B_DIR}/cacerts_all" ]; then
    echo "cacerts_all dir not exists" >&2
    exit 1
fi
CACERTS_ALL_A_DIR=${CERT_A_DIR}/cacerts_all
CACERTS_ALL_B_DIR=${CERT_B_DIR}/cacerts_all

make_client_chain
if [ $? -ne 0 ]; then
    echo "make_client_chain failed" >&2
    exit 1
fi 

make_server_chain
if [ $? -ne 0 ]; then
    echo "make_server_chain failed" >&2
    exit 1
fi

make_cert_dir
if [ $? -ne 0 ]; then
    echo "make_cert_dir failed" >&2
    exit 1
fi

make_ab
if [ $? -ne 0 ]; then
    echo "make_ab failed" >&2
    exit 1
fi
exit 0
