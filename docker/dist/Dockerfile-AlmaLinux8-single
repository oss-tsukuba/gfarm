FROM almalinux:8

RUN dnf -y update \
 && dnf -y install \
    git wget rpm-build \
    gcc make libtool \
    openssl-devel postgresql-devel \
    rdma-core-devel \
    postgresql postgresql-server \
    python3 ruby \
 && dnf -y install epel-release \
 && dnf -y install \
    globus-gssapi-gsi-devel \
    python3-docopt python3-schema python3-tqdm python3-flake8 \
    fuse fuse-devel libacl-devel \
    cyrus-sasl-devel scitokens-cpp-devel \
    procps

ADD . /root/gfarm

CMD ["/sbin/init"]
