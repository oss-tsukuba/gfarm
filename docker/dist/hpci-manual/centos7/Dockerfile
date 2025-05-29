FROM centos:7

ARG USERNAME=foo
ARG UID=1000

# 2024-07-01: original yum repository was deprecated
RUN for i in /etc/yum.repos.d/CentOS-*.repo; do \
      cp -p ${i} ${i}.bak && \
      sed -e '/^mirrorlist=/s/^/#/' \
	  -e '/^#baseurl=/s|^#baseurl=http://mirror|baseurl=http://vault|' \
	    ${i}.bak >${i}; \
    done

RUN yum -y update \
 && yum -y groupinstall 'Development Tools' \
 && yum -y install epel-release \
 && yum -y install \
    cyrus-sasl-devel scitokens-cpp-devel \
    golang python3 \
    python36-docopt python36-schema \
    fuse fuse-devel libacl-devel \
    jq wget curl git \
 && yum -y install openssl11-devel

RUN mkdir -p /etc/grid-security/certificates \
 && cd /etc/grid-security/certificates \
 && wget https://www.hpci-office.jp/info/download/attachments/425328655/21d9c8b3.0 \
 && mkdir -p /etc/pki/tls/certs \
 && ln -s /etc/grid-security/certificates /etc/pki/tls/certs/gfarm

ARG OSSURL=https://github.com/oss-tsukuba
ARG PKG=jwt-logon
ARG VER=1.1.0

RUN cd \
 && wget --content-disposition $OSSURL/$PKG/archive/$VER.tar.gz \
 && tar pxf $PKG-$VER.tar.gz \
 && cd $PKG-$VER \
 && make PREFIX=/usr/local install

ARG PKG=jwt-agent
ARG VER=1.1.0

RUN cd \
 && wget --content-disposition $OSSURL/$PKG/archive/$VER.tar.gz \
 && tar pxf $PKG-$VER.tar.gz \
 && cd $PKG-$VER \
 && make \
 && make PREFIX=/usr/local install

ARG PKG=cyrus-sasl-xoauth2-idp
ARG VER=1.0.2

RUN cd \
 && wget --content-disposition $OSSURL/$PKG/archive/$VER.tar.gz \
 && tar pxf $PKG-$VER.tar.gz \
 && cd $PKG-$VER \
 && ./autogen.sh \
 && ./configure --libdir=$(pkg-config --variable=libdir libsasl2) \
 && make \
 && make install \
 && echo xoauth2_user_claim: hpci.id > $(pkg-config --variable=libdir libsasl2)/sasl2/gfarm-client.conf

ARG PKG=gfarm
ARG VER=2.8.7

RUN cd \
 && wget --content-disposition $OSSURL/$PKG/archive/$VER.tar.gz \
 && tar pxf $PKG-$VER.tar.gz \
 && cd $PKG-$VER \
 && ./configure --sysconfdir=/etc --enable-xmlattr --enable-tls13 --enable-cyrus-sasl --with-openssl=openssl11 \
 && make \
 && make install

RUN cd \
 && wget https://www.hpci-office.jp/info/download/attachments/69471402/get_gfarm2conf.sh \
 && sh ./get_gfarm2conf.sh \
 && chmod 644 /etc/gfarm2.conf \
 && if ! grep sasl /etc/gfarm2.conf 2>&1 /dev/null; then echo 'auth enable sasl *' >>/etc/gfarm2.conf; fi

ARG PKG=gfarm2fs
ARG VER=1.2.22

RUN cd \
 && wget --content-disposition $OSSURL/$PKG/archive/$VER.tar.gz \
 && tar pxf $PKG-$VER.tar.gz \
 && cd $PKG-$VER \
 && ./configure --with-gfarm=/usr/local \
 && make \
 && make install

RUN useradd $USERNAME
