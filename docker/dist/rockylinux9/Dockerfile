FROM rockylinux/rockylinux:9

ARG USERNAME=foo
ARG UID=1000

RUN dnf -y update \
 && dnf -y install \
    git wget rpm-build sudo passwd \
    gcc make libtool \
    openssl-devel postgresql-devel \
    rdma-core-devel \
    postgresql postgresql-server \
    python3 ruby \
    libxslt docbook-style-xsl \
 && dnf -y install epel-release \
 && dnf config-manager --set-enabled crb \
 && dnf -y install \
    globus-gssapi-gsi-devel \
    python3-docopt python3-schema python3-tqdm python3-flake8 \
    fuse fuse-devel libacl-devel \
    procps gdb valgrind \
    myproxy globus-simple-ca \
    cyrus-sasl-devel scitokens-cpp-devel \
    golang jq \
    pandoc attr

RUN \
 # sshd
 dnf -y install openssh-server \
 # sshd_config
 && mv /etc/ssh/sshd_config /etc/ssh/sshd_config.orig \
 && awk ' \
    /#PermitEmptyPasswords no/ { print "PermitEmptyPasswords yes"; next } \
    /UsePAM yes/ { print "UsePAM no"; next } \
    { print }' /etc/ssh/sshd_config.orig > /etc/ssh/sshd_config \
 # ssh_config
 && printf '%s\n' \
    'Host *' \
    '    StrictHostKeyChecking no' \
    > /etc/ssh/ssh_config.d/ignore-host-key.conf \
 && systemctl enable sshd

RUN useradd -m -u $UID -s /bin/bash $USERNAME \
 && echo "$USERNAME ALL=(ALL:ALL) NOPASSWD: ALL" >> /etc/sudoers.d/$USERNAME \
 # delete passwd
 && passwd -d $USERNAME

RUN useradd -m _gfarmfs \
 && useradd -m _gfarmmd

RUN mv /etc/sudoers /etc/sudoers.orig \
 && awk '/secure_path/ { print $0 ":/usr/local/bin"; next}; {print}' \
	/etc/sudoers.orig > /etc/sudoers

CMD ["/sbin/init"]
