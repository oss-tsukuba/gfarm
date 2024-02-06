FROM centos:7

ARG USERNAME=foo
ARG UID=1000

RUN yum -y update \
 && yum -y install \
    git wget rpm-build sudo passwd \
    gcc make libtool \
    python3 ruby \
    libxslt docbook-style-xsl \
    epel-release \
 && yum -y install \
    python36-docopt python36-schema python36-tqdm \
 && pip3 install flake8 \
 && yum -y install \
    fuse fuse-devel libacl-devel \
    procps valgrind \
    openssl11-devel openssl11 \
    cyrus-sasl-devel scitokens-cpp-devel \
    golang jq pandoc curl attr

COPY docker/dist/centos7a/globus-patch.sh /

RUN cd \
 && wget https://repo.gridcf.org/gct6/sources/gct-6.2.1653033972.tar.gz \
 && tar zxfp gct-6.2.1653033972.tar.gz \
 && cd gct-6.2.1653033972 \
 && /globus-patch.sh \
 && ./configure --sysconfdir=/etc --disable-gridftp --disable-gram5 --disable-gsi-openssh \
 && make gsi myproxy install

COPY docker/dist/centos7a/globus.sh /etc/profile.d/

RUN \
 # sshd
 yum -y install openssh-server \
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
    >> /etc/ssh/ssh_config \
 && systemctl enable sshd

RUN useradd -m -u $UID -s /bin/bash $USERNAME \
 && echo "$USERNAME ALL=(ALL:ALL) NOPASSWD: ALL" >> /etc/sudoers.d/$USERNAME \
 # delete passwd
 && passwd -d $USERNAME

RUN useradd -m _gfarmfs \
 && useradd -m _gfarmmd

RUN mv /etc/sudoers /etc/sudoers.orig \
 && awk '/secure_path/ { print $0 ":/usr/local/bin:/usr/local/globus-6/sbin:/usr/local/globus-6/bin"; next}; {print}' \
        /etc/sudoers.orig > /etc/sudoers

CMD ["/sbin/init"]
