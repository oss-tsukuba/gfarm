#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
CONF_DIR=${TOP_DIR}/conf

OUTPUT_DIR=${PWD}/out
ISSUER_CA=""
DAYS=36500
CA_TYPE="root"
CRT_FILE=""
CRL_FILE=""

## Funcs. ##
usage() {
     cat << EOS >&2
Usage: `basename $0` [OPTION]...
Generate a CRL.

  OPTION:
    -r                   Generate Root certificate. (default, Required opts: -I, -C, -R)
    -i                   Generate Intermediate certificate. (Required opts: -I, -C, -R)
    -s                   Generate Server certificate. (Required opts: -I, -C, -R)
    -c                   Generate Client certificate. (Required opts: -I, -C, -R)
    -d DAYS              Expiration. (default: ${DAYS})
    -o OUTPUT_DIR        Output dir. (default: ${OUTPUT_DIR})
    -C CRT_FILE          Target certificate file. (default: empty)
    -R CRL_FILE          Output CRL file name. (default: empty)
    -I ISSUER_CA         Issuer CA. (default: empty)
    -h                   Help.
EOS
    exit 0
}

puts_error() {
    echo "ERR: $1" >&2
}

## Opts. ##
while getopts d:o:x:C:R:I:risch OPT; do
    case ${OPT} in
        d) DAYS=${OPTARG};;
        o) OUTPUT_DIR=${OPTARG};;
        C) CRT_FILE=`readlink -f ${OPTARG}`;;
        R) CRL_FILE=${OPTARG};;
        I) ISSUER_CA=`readlink -f ${OPTARG}`;;
        r) CA_TYPE="root";;
        i) CA_TYPE="inter";;
        s) CA_TYPE="server";;
        c) CA_TYPE="client";;
        h) usage;;
        *) usage;;
    esac
done
shift `expr $OPTIND - 1`

if [ -z "${ISSUER_CA}" -o -z "${CRT_FILE}" -o -z "${CRL_FILE}" ]; then
    puts_error "-I, -C, -R are required options"
    usage
fi

case ${CA_TYPE} in
    "root")
        CONF=${CONF_DIR}/openssl_ca.cnf
        ;;
    "inter")
        CONF=${CONF_DIR}/openssl_ca.cnf
        ;;
    "server")
        CONF=${CONF_DIR}/openssl_server.cnf
        ;;
    "client")
        CONF=${CONF_DIR}/openssl_client.cnf
        ;;
esac

pushd ${OUTPUT_DIR}

if [ ! -f ./crlnumber ]; then
    echo 00 > ./crlnumber
fi

openssl ca -config ${CONF} -revoke ${CRT_FILE} \
    -keyfile ${ISSUER_CA}/ca.key -cert ${ISSUER_CA}/ca.crt
if [ $? -ne 0 ]; then
    puts_error "openssl revoke: $?"
    exit 1
fi

openssl ca -config ${CONF} -gencrl -crldays ${DAYS} \
    -out ${CRL_FILE} -keyfile ${ISSUER_CA}/ca.key -cert ${ISSUER_CA}/ca.crt
if [ $? -ne 0 ]; then
    puts_error "openssl gencrl: $?"
    exit 1
fi

popd
