#!/bin/sh

## Funcs. ##
usage() {
     cat << EOS >&2
Usage: `basename $0` [OPTION]... CERTS_DIR
Generate a hash file of certificate.

  CERTS_DIR:     Certificates directory.

  OPTION:
    -h           Help.
EOS
    exit 0
}


## Opts. ##
while getopts h OPT; do
    case ${OPT} in
        h) usage;;
        *) usage;;
    esac
done
shift `expr $OPTIND - 1`


## Main. ##
DIR=$1

if [ -z "${DIR}" ]; then
    usage
fi

cd ${DIR}

ls *\.crt >/dev/null 2>&1
if [ $? -eq 0 ]; then
    ls *\.crt | xargs -I {} sh -c 'ln -s {} "`openssl x509 -hash -noout -in {}`.0"'
fi

ls *\.crl >/dev/null 2>&1
if [ $? -eq 0 ]; then
    ls *\.crl | xargs -I {} sh -c 'ln -s {} "`openssl crl -hash -noout -in {}`.r0"'
fi
