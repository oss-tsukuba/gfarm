#!/bin/sh

TOP_DIR=`dirname $0`
TOP_DIR=`cd ${TOP_DIR}; pwd`
LIB_DIR=${TOP_DIR}/lib
OUTPUT_DIR=${PWD}/out
TEST_SUITE="revocation"
PASS=test

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

gen_client_cert() {
    TARGET=$1
    SERIAL=$2
    SUBJECT_CLIENT_CERT=$3

    if [ -z "${TARGET}" -o -z "${SERIAL}" -o -z "${SUBJECT_CLIENT_CERT}" ]; then
        puts_error "TARGET, SERIAL and SUBJECT_CLIENT_CERT are required arguments."
        return 1
    fi

    OUTPUT_CAS_DIR="${OUTPUT_DIR}/${TARGET}/cas"
    OUTPUT="${OUTPUT_DIR}/${TARGET}/client"

    mkdir -p "${OUTPUT_CAS_DIR}" && mkdir -p "${OUTPUT}"
    if [ $? -ne 0 ]; then
        puts_error "mkdir"
        return 1
    fi

    # gen Client cert.
    ${TOP_DIR}/gen_cert.sh -c \
        -I "${OUTPUT_DIR}/${TARGET}/cas/inter_ca_3" \
        -o "${OUTPUT_CAS_DIR}" -S "${SUBJECT_CLIENT_CERT}" -p ${PASS}
    if [ $? -ne 0 ]; then
        puts_error "gen Client cert: $?"
        return 1
    fi
    cp ${OUTPUT_CAS_DIR}/client/client.crt ${OUTPUT}/client${SERIAL}.crt && \
        cp ${OUTPUT_CAS_DIR}/client/client.key ${OUTPUT}/client${SERIAL}.key
    if [ $? -ne 0 ]; then
        puts_error "cp"
        return 1
    fi

    return 0
}

gen_crls() {
    TARGET=$1

    if [ -z "${TARGET}" ]; then
        puts_error "TARGET is required arguments."
        return 1
    fi

    OUTPUT=${OUTPUT_DIR}/${TARGET}

    # Generate CRL fot root.
    ${TOP_DIR}/gen_crl.sh -r -I "${OUTPUT}/cas/root_ca/" \
        -o "${OUTPUT}/cas/root_ca/" -C "${OUTPUT}/cacerts_all/root_ca.crt" \
        -R "ca.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh root"
        return 1
    fi

    # Generate CRL fot inter CA 1.
    ${TOP_DIR}/gen_crl.sh -i -I "${OUTPUT}/cas/root_ca/" \
        -o "${OUTPUT}/cas/inter_ca_1/" -C "${OUTPUT}/cacerts_all/inter_ca_1.crt" \
        -R "ca.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh inter ca 1"
        return 1
    fi

    # Generate CRL fot inter CA 2.
    ${TOP_DIR}/gen_crl.sh -i -I "${OUTPUT}/cas/inter_ca_1/" \
        -o "${OUTPUT}/cas/inter_ca_2/" -C "${OUTPUT}/cacerts_all/inter_ca_2.crt" \
        -R "ca.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh inter ca 2"
        return 1
    fi

    # Generate CRL fot inter CA 3.
    ${TOP_DIR}/gen_crl.sh -i -I "${OUTPUT}/cas/inter_ca_2/" \
        -o "${OUTPUT}/cas/inter_ca_3/" -C "${OUTPUT}/cacerts_all/inter_ca_3.crt" \
        -R "ca.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh inter ca 3"
        return 1
    fi

    # backup origin index.txt for client.
    cp "${OUTPUT}/cas/client/index.txt" "${OUTPUT}/cas/client/index.txt.ori"

    # Generate CRL fot client 1.
    ${TOP_DIR}/gen_crl.sh -c -I "${OUTPUT}/cas/inter_ca_3/" \
        -o "${OUTPUT}/cas/client/" -C "${OUTPUT}/client/client.crt" \
        -R "client1.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh client 1"
        return 1
    fi

    # Generate CRL fot client 1+2.
    ${TOP_DIR}/gen_crl.sh -c -I "${OUTPUT}/cas/inter_ca_3/" \
        -o "${OUTPUT}/cas/client/" -C "${OUTPUT}/client/client2.crt" \
        -R "client1_2.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh client 1+2"
        return 1
    fi

    # Generate CRL fot client 1+2+3.
    ${TOP_DIR}/gen_crl.sh -c -I "${OUTPUT}/cas/inter_ca_3/" \
        -o "${OUTPUT}/cas/client/" -C "${OUTPUT}/client/client3.crt" \
        -R "client1_2_3.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh client 1+2+3"
        return 1
    fi

    # Generate CRL fot client 2.
    cp "${OUTPUT}/cas/client/index.txt.ori" "${OUTPUT}/cas/client/index.txt"
    ${TOP_DIR}/gen_crl.sh -c -I "${OUTPUT}/cas/inter_ca_3/" \
        -o "${OUTPUT}/cas/client/" -C "${OUTPUT}/client/client2.crt" \
        -R "client2.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh client 2"
        return 1
    fi

    # Generate CRL fot client 3.
    cp "${OUTPUT}/cas/client/index.txt.ori" "${OUTPUT}/cas/client/index.txt"
    ${TOP_DIR}/gen_crl.sh -c -I "${OUTPUT}/cas/inter_ca_3/" \
        -o "${OUTPUT}/cas/client/" -C "${OUTPUT}/client/client3.crt" \
        -R "client3.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh client 3"
        return 1
    fi

    # Generate CRL fot server.
    ${TOP_DIR}/gen_crl.sh -s -I "${OUTPUT}/cas/inter_ca_2/" \
        -o "${OUTPUT}/cas/server/" -C "${OUTPUT}/server/server.crt" \
        -R "server.crl"
    if [ $? -ne 0 ]; then
        puts_error "gen_crl.sh server"
        return 1
    fi

    return 0
}

cp_crls() {
    TARGET=$1

    if [ -z "${TARGET}" ]; then
        puts_error "TARGET is required arguments."
        return 1
    fi

    OUTPUT=${OUTPUT_DIR}/${TARGET}

    mkdir -p "${OUTPUT}"/crls/{root,inter_ca_1,inter_ca_2,inter_ca_3,server,client1,client2,client3,client1_2,client1_2_3}
    if [ $? -ne 0 ]; then
        puts_error "mkdir"
        return 1
    fi

    cp "${OUTPUT}/cas/root_ca/ca.crl" "${OUTPUT}/crls/root/" && \
        cp "${OUTPUT}/cas/inter_ca_1/ca.crl" "${OUTPUT}/crls/inter_ca_1/" && \
        cp "${OUTPUT}/cas/inter_ca_2/ca.crl" "${OUTPUT}/crls/inter_ca_2/" && \
        cp "${OUTPUT}/cas/inter_ca_3/ca.crl" "${OUTPUT}/crls/inter_ca_3/" && \
        cp "${OUTPUT}/cas/server/server.crl" "${OUTPUT}/crls/server/"&& \
        cp "${OUTPUT}/cas/client/client1.crl" "${OUTPUT}/crls/client1/" && \
        cp "${OUTPUT}/cas/client/client2.crl" "${OUTPUT}/crls/client2/" && \
        cp "${OUTPUT}/cas/client/client3.crl" "${OUTPUT}/crls/client3/" && \
        cp "${OUTPUT}/cas/client/client1_2.crl" "${OUTPUT}/crls/client1_2/" && \
        cp "${OUTPUT}/cas/client/client1_2_3.crl" "${OUTPUT}/crls/client1_2_3/"
    if [ $? -ne 0 ]; then
        puts_error "cp"
        return 1
    fi

    return 0
}

gen_hash() {
    TARGET=$1

    if [ -z "${TARGET}" ]; then
        puts_error "TARGET is required arguments."
        return 1
    fi

    OUTPUT=${OUTPUT_DIR}/${TARGET}

    DIRS="root inter_ca_1 inter_ca_2 inter_ca_3 server client1 client2 client3 client1_2 client1_2_3"
    for DIR in ${DIRS}; do
        olddir="`pwd`"
        cd "${OUTPUT}/crls/$DIR"

        ls *\.crl | xargs -I {} sh -c 'ln -s {} "`openssl crl -noout -hash < {}`.r0"'
        if [ $? -ne 0 ]; then
            puts_error "CRL symlink"
            return 1
        fi

        cd "${olddir}"
    done

    return 0
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


## Main. ##

# Generated certs.
gen_certs "${TEST_SUITE}" "${TOP_DIR}" "${OUTPUT_DIR}"
if [ $? -ne 0 ]; then
    puts_error "gen_certs"
    exit 1
fi

# set test data.
OUTPUT_DIR="${OUTPUT_DIR}/${TEST_SUITE}"

TARGETS="A B"
for TARGET in ${TARGETS};do
    for SEQ in `seq 2 3`; do
        gen_client_cert ${TARGET} ${SEQ} "/C=JP/ST=Tokyo/O=SRA/CN=gfarm_client_${TARGET}_${SEQ}.sra.co.jp"
        if [ $? -ne 0 ]; then
            puts_error "gen_client_cert"
            exit 1
        fi
    done

    gen_crls ${TARGET}
    if [ $? -ne 0 ]; then
        puts_error "gen_crls"
       exit 1
    fi

    cp_crls ${TARGET}
    if [ $? -ne 0 ]; then
        puts_error "cp_crls"
        exit 1
    fi

    gen_hash ${TARGET}
    if [ $? -ne 0 ]; then
        puts_error "gen_hash"
        exit 1
    fi
done

exit 0
