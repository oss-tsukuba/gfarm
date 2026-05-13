FROM rockylinux/rockylinux:9

ARG USERNAME=foo

RUN dnf -y upgrade \
 && dnf -y groupinstall 'Development Tools' \
 && dnf -y install epel-release \
 && dnf -y install \
    libtool openssl-devel libpq-devel \
    cyrus-sasl-devel scitokens-cpp-devel \
    golang python3 ruby \
    python3-docopt python3-schema \
 && dnf config-manager --set-enabled crb \
 && dnf -y install \
    fuse fuse-devel libacl-devel \
    wget jq git

RUN mkdir -p /etc/pki/tls/certs/gfarm \
 && cd /etc/pki/tls/certs/gfarm \
 && wget https://www.hpci-office.jp/info/download/attachments/425328655/21d9c8b3.0

ARG OSSURL=https://github.com/oss-tsukuba
ARG PKG=jwt-logon
ARG VER=1.1.0

RUN cd \
 && wget $OSSURL/$PKG/releases/download/$VER/$PKG-$VER-1.src.rpm \
 && rpmbuild --rebuild $PKG-$VER-1.src.rpm \
 && rpm -Uvh rpmbuild/RPMS/*/$PKG-*

ARG PKG=jwt-agent
ARG VER=1.1.1

RUN cd \
 && wget $OSSURL/$PKG/releases/download/$VER/$PKG-$VER-1.src.rpm \
 && rpmbuild --rebuild $PKG-$VER-1.src.rpm \
 && rpm -Uvh rpmbuild/RPMS/*/$PKG-*

ARG PKG=cyrus-sasl-xoauth2-idp
ARG VER=1.1.0

RUN cd \
 && wget $OSSURL/$PKG/releases/download/$VER/$PKG-$VER-1.src.rpm \
 && rpmbuild --rebuild $PKG-$VER-1.src.rpm \
 && rpm -Uvh rpmbuild/RPMS/*/$PKG-* \
 && echo xoauth2_user_claim: hpci.id > $(pkg-config --variable=libdir libsasl2)/sasl2/gfarm-client.conf

ARG PKG=gfarm
ARG VER=2.8.9

RUN cd \
 && wget $OSSURL/$PKG/releases/download/$VER/$PKG-$VER-1.src.rpm \
 && GFARM_CONFIGURE_OPTION="-enable-xmlattr --enable-tls13 --enable-cyrus-sasl" rpmbuild --rebuild $PKG-$VER-1.src.rpm \
 && rpm -Uvh rpmbuild/RPMS/*/$PKG-*

RUN cd \
 && wget https://www.hpci-office.jp/info/download/attachments/69471402/get_gfarm2conf.sh \
 && sh ./get_gfarm2conf.sh \
 && chmod 644 /etc/gfarm2.conf \
 && if ! grep sasl /etc/gfarm2.conf 2>&1 /dev/null; then echo 'auth enable sasl *' >>/etc/gfarm2.conf; fi

ARG PKG=gfarm2fs
ARG VER=1.2.22

RUN cd \
 && wget $OSSURL/$PKG/releases/download/$VER/$PKG-$VER-1.src.rpm \
 && rpmbuild --rebuild $PKG-$VER-1.src.rpm \
 && rpm -Uvh rpmbuild/RPMS/*/$PKG-*

RUN useradd $USERNAME
