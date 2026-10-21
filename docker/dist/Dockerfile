FROM almalinux:8

RUN dnf -y upgrade almalinux-release \
 && dnf -y update \
 && dnf -y install \
    git wget rpm-build \
    gcc make libtool \
    openssl-devel postgresql-devel \
    rdma-core-devel \
    postgresql postgresql-server \
    python3 ruby golang \
 && dnf -y install epel-release \
 && dnf -y install \
    globus-gssapi-gsi-devel \
    python3-docopt python3-schema python3-tqdm python3-flake8 \
    fuse3 fuse3-devel libacl-devel \
    cyrus-sasl-devel scitokens-cpp-devel \
    procps

ADD . /root/gfarm

# shut down systemd cleanly by "docker stop"
STOPSIGNAL SIGRTMIN+3
CMD ["/sbin/init"]
