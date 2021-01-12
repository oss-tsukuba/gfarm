#!/bin/sh

puts_error() {
    echo "ERR: $1" >&2
}

copy_dir() {
    SRC_DIR=$1
    DST_DIR=$2

    if [ -z "${SRC_DIR}" -o -z "${DST_DIR}" ]; then
        puts_error "SRC_DIR, DST_DIR are required arguments."
        return 1
    fi

    mkdir -p "${DST_DIR}"
    if [ $? -ne 0 ]; then
        return 1
    fi

    (cd "${SRC_DIR}/"; tar cf - ./) | (cd "${DST_DIR}/"; tar xvf -)
    if [ $? -ne 0 ]; then
        return 1
    fi

    return 0
}

copy_certs() {
    TEST_SUITE=$1
    GEN_CERTS_DIR=$2
    OUTPUT_DIR=$3

    if [ -z "${TEST_SUITE}" -o \
        -z "${GEN_CERTS_DIR}" -o \
        -z "${OUTPUT_DIR}" ]; then
        puts_error "TEST_SUITE, GEN_CERTS_DIR and OUTPUT_DIR" \
            " are required arguments."
        return 1
    fi

    copy_dir "${GEN_CERTS_DIR}/" "${OUTPUT_DIR}/${TEST_SUITE}/"
    if [ $? -ne 0 ]; then
        return 1
    fi

    return 0
}

gen_certs() {
    TEST_SUITE=$1
    GEN_CERTS_ALL_PATH=$2
    OUTPUT_DIR=$3

    if [ -z "${TEST_SUITE}" -o \
        -z "${OUTPUT_DIR}" ]; then
        puts_error "TEST_SUITE and OUTPUT_DIR" \
            " are required arguments."
        return 1
    fi

    ${GEN_CERTS_ALL_PATH}/gen_certs_all.sh -o "${OUTPUT_DIR}/${TEST_SUITE}/A"
    ${GEN_CERTS_ALL_PATH}/gen_certs_all.sh -o "${OUTPUT_DIR}/${TEST_SUITE}/B" -X 2
}
