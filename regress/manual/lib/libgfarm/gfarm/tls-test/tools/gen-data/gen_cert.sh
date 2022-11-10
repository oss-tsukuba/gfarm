#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
CONF_DIR=${TOP_DIR}/conf

OUTPUT_DIR=${PWD}/out
ISSUER_CA=""
INTER_CA_SUFFIX=""
SUBJECT_SUFFIX="1"
DIR_NAME=""
CA_TYPE="root"
DAYS=36500
IS_INTERACTIVE_INPUT_PASS="FALSE"
IS_GEN_CRL="FALSE"
PASS=test

## Funcs. ##
usage() {
     cat << EOS >&2
Usage: `basename $0` [OPTION]...
Generate a certificate.

  OPTION:
    -r                   Generate Root certificate. (default)
    -i                   Generate Intermediate certificate. (Required opts: -x, -I)
    -s                   Generate Server certificate. (Required opts: -I)
    -c                   Generate Client certificate. (Required opts: -I)
    -d DAYS              Expiration. (default: ${DAYS})
    -o OUTPUT_DIR        Output dir. (default: ${OUTPUT_DIR})
    -p PASS              Password for client private key.
                         Only the client private key password is valid.
                         If you do not specify this option, the password is "${PASS}".
    -P                   Enable interactive password input.
                         Only the client private key password is valid.
                         If you do not specify this option, the password is "${PASS}".
                         (default: ${IS_INTERACTIVE_INPUT_PASS})
    -R                   Generate GRL for "No Revoked Certificates".
                         (default: ${IS_GEN_CRL})
    -x INTER_CA_SUFFIX   Suffix of Intermediate CA. (default: empty)
    -X SUBJECT_SUFFIX    Suffix of subject. (default: ${SUBJECT_SUFFIX})
    -I ISSUER_CA         Issuer CA. (default: empty)
    -S SUBJ              Subject.
                         Last subject of the server certificate and client certificate must be CN.
    -D DIR_NAME          Certificate directory under 'OUTPUT_DIR'.
                         (default: root_ca,server,client,inter_ca_XX)
    -h                   Help.
EOS
    exit 0
}

puts_error() {
    echo "ERR: $1" >&2
}

gen_root() {
    openssl req -passout pass:${PASS} -new -keyout ca.key -newkey rsa:2048 -out ca.csr \
        -extensions v3_ca -subj "${SUBJ}" -config ${CONF}
    if [ $? -ne 0 ]; then
        puts_error "openssl req: $?"
        return 1
    fi

    openssl rsa -passin pass:${PASS} -in ca.key -out ca.key
    if [ $? -ne 0 ]; then
        puts_error "openssl rsa: $?"
        return 1
    fi

    openssl ca -config ${CONF} -batch -extensions v3_ca \
        -out ca.crt -in ca.csr -selfsign -keyfile ca.key -days ${DAYS}
    if [ $? -ne 0 ]; then
        puts_error "openssl ca: $?"
        return 1
    fi

    if [ "x${IS_GEN_CRL}" = "xTRUE" ] ; then
        openssl ca -config ${CONF} -gencrl -crldays ${DAYS} -out ca.crl \
            -keyfile ca.key -cert ca.crt
        if [ $? -ne 0 ]; then
            puts_error "openssl ca -gencrl: $?"
            return 1
        fi
    fi
}

gen_inter() {
    openssl req -passout pass:${PASS} -new -keyout ca.key -newkey rsa:2048 -out ca.csr \
        -extensions v3_ca -subj "${SUBJ}" -config ${CONF}

    if [ $? -ne 0 ]; then
        puts_error "openssl req: $?"
        return 1
    fi

    openssl rsa -passin pass:${PASS} -in ca.key -out ca.key
    if [ $? -ne 0 ]; then
        puts_error "openssl rsa: $?"
        return 1
    fi

    openssl ca -config ${CONF} -batch -extensions v3_ca \
        -out ca.crt -in ca.csr \
        -keyfile ${ISSUER_CA}/ca.key \
        -cert ${ISSUER_CA}/ca.crt -days ${DAYS}
    if [ $? -ne 0 ]; then
        puts_error "openssl ca: $?"
        return 1
    fi

    if [ "x${IS_GEN_CRL}" = "xTRUE" ] ; then
        openssl ca -config ${CONF} -gencrl -crldays ${DAYS} -out ca.crl \
            -keyfile ca.key -cert ca.crt
        if [ $? -ne 0 ]; then
            puts_error "openssl ca -gencrl: $?"
            return 1
        fi
    fi
}

gen_server() {
    openssl genrsa -passout pass:${PASS} -aes256 -out server.key 2048
    if [ $? -ne 0 ]; then
        puts_error "openssl genrsa: $?"
        return 1
    fi

    openssl rsa -passin pass:${PASS} -in server.key -out server.key
    if [ $? -ne 0 ]; then
        puts_error "openssl rsa: $?"
        return 1
    fi

    openssl req -new -key server.key -out server.csr -extensions usr_cert \
        -subj "${SUBJ}" -config ${CONF}
    if [ $? -ne 0 ]; then
        puts_error "openssl req: $?"
        return 1
    fi

    openssl ca -config ${CONF} -batch -out server.crt -in server.csr \
        -keyfile ${ISSUER_CA}/ca.key -cert ${ISSUER_CA}/ca.crt -days ${DAYS}
    if [ $? -ne 0 ]; then
        puts_error "openssl ca: $?"
        return 1
    fi
}

gen_client() {
    openssl genrsa -passout pass:${PASS} -aes256 -out client_encrypted.key 2048
    if [ $? -ne 0 ]; then
        puts_error "openssl genrsa: $?"
        return 1
    fi

    openssl rsa -passin pass:${PASS} -in client_encrypted.key -out client.key
    if [ $? -ne 0 ]; then
        puts_error "openssl rsa: $?"
        return 1
    fi

    openssl req -new -key client.key -out client.csr -extensions usr_cert \
        -subj "${SUBJ}" -config ${CONF}
    if [ $? -ne 0 ]; then
        puts_error "openssl req: $?"
        return 1
    fi

    openssl ca -config ${CONF} -batch -out client.crt -in client.csr \
        -keyfile ${ISSUER_CA}/ca.key -cert ${ISSUER_CA}/ca.crt -days ${DAYS}
    if [ $? -ne 0 ]; then
        puts_error "openssl ca: $?"
        return 1
    fi
}


## Opts. ##
while getopts riscd:x:o:p:PRX:I:S:C:D:h OPT; do
    case ${OPT} in
        r) CA_TYPE="root";;
        i) CA_TYPE="inter";;
        s) CA_TYPE="server";;
        c) CA_TYPE="client";;
        d) DAYS=${OPTARG};;
        o) OUTPUT_DIR=${OPTARG};;
        x) INTER_CA_SUFFIX=${OPTARG};;
        p) PASS=${OPTARG};;
        P) IS_INTERACTIVE_INPUT_PASS="TRUE";;
        R) IS_GEN_CRL="TRUE";;
        X) SUBJECT_SUFFIX=${OPTARG};;
        I) ISSUER_CA=`readlink -f ${OPTARG}`;;
        D) DIR_NAME=${OPTARG};;
        S) SUBJ=${OPTARG};;
        h) usage;;
        *) usage;;
    esac
done
shift `expr $OPTIND - 1`


## Main. ##

# set vals.
case ${CA_TYPE} in
    "root")
        if [ -z "${SUBJ}" ]; then
            SUBJ="/C=JP/ST=Tokyo/O=SRA/CN=root_ca${SUBJECT_SUFFIX}.sra.co.jp"
        fi
        DIR=${DIR_NAME:-"root_ca"}
        CONF=${CONF_DIR}/openssl_ca.cnf
        ;;
    "inter")
        if [ -z "${INTER_CA_SUFFIX}" -o -z "${ISSUER_CA}" ]; then
            puts_error "-x is a required option"
            puts_error "-I is a required option"
            exit 1
        fi

        if [ -z "${SUBJ}" ]; then
            SUBJ="/C=JP/ST=Tokyo/O=SRA/CN=inter_ca${SUBJECT_SUFFIX}_${INTER_CA_SUFFIX}.sra.co.jp"
        fi
        DIR=${DIR_NAME:-"inter_ca_${INTER_CA_SUFFIX}"}
        CONF=${CONF_DIR}/openssl_ca.cnf
        ;;
    "server")
        if [ -z "${ISSUER_CA}" ]; then
            puts_error "-I is a required option"
            exit 1
        fi

        if [ -z "${SUBJ}" ]; then
            SUBJ="/C=JP/ST=Tokyo/O=SRA/CN=gfarm_server${SUBJECT_SUFFIX}.sra.co.jp"
        fi

        IS_LAST_CN=`echo "${SUBJ}" | awk -F/ '{print $NF}' | awk 'match($0, /^CN=.*/)'`
        if [ -z "${IS_LAST_CN}" ]; then
            puts_error "Last of the subject must be CN"
            exit 1
        fi

        DIR=${DIR_NAME:-"server"}
        CONF=${CONF_DIR}/openssl_server.cnf
        ;;
    "client")
        if [ -z "${ISSUER_CA}" ]; then
            puts_error "-I is a required option"
            exit 1
        fi

        if [ -z "${SUBJ}" ]; then
            SUBJ="/C=JP/ST=Tokyo/O=SRA/CN=gfarm_client${SUBJECT_SUFFIX}.sra.co.jp"
        fi

        IS_LAST_CN=`echo "${SUBJ}" | awk -F/ '{print $NF}' | awk 'match($0, /^CN=.*/)'`
        if [ -z "${IS_LAST_CN}" ]; then
            puts_error "Last of the subject must be CN"
            exit 1
        fi

        # input passwd.
        if [ "x${IS_INTERACTIVE_INPUT_PASS}" = "xTRUE" ] ; then
            read -sp "Password of client private key: " PASS
        fi

        DIR=${DIR_NAME:-"client"}
        CONF=${CONF_DIR}/openssl_client.cnf
        ;;
esac

mkdir -p ${OUTPUT_DIR}/${DIR}
cd ${OUTPUT_DIR}/${DIR}

# cretate newcerts/, index.txt, serial
mkdir -p newcerts
if [ ! -f index.txt ]; then
    touch index.txt
fi
if [ ! -f serial ]; then
    echo '01' > serial
fi

if [ ! -f crlnumber ]; then
    echo '00' > crlnumber
fi

# call gen func.
gen_${CA_TYPE}
RET=$?

find ./ -name "*\.key" | xargs -I {} chmod 600 {}

exit ${RET}
