#!/bin/bash

set -eux

BASEDIR=$(dirname $(realpath $0))
FUNCTIONS=${BASEDIR}/functions.sh
. ${FUNCTIONS}

NAME=gfarm-gridftp-dsi

# from git@github.com:oss-tsukuba/gfarm-gridftp-dsi.git
WORKDIR=${MNTDIR}/work/${NAME}

PACKAGES="globus-gridftp-server-devel globus-gridftp-server-progs"

setup_for_centos() {
    sudo \
        http_proxy=${http_proxy:-} \
        https_proxy=${https_proxy:-} \
        yum -y update
    sudo \
        http_proxy=${http_proxy:-} \
        https_proxy=${https_proxy:-} \
        yum -y install $PACKAGES
}

# Workaround for openSUSE:
# globus-gridftp-server packages are not available in the openSUSE
# repositories. Build globus_gridftp_server from the official GCT
# source release instead.
# See: https://download.opensuse.org/app/package/globus-gridftp-server-progs
setup_for_opensuse() {
    sudo \
        http_proxy=${http_proxy:-} \
        https_proxy=${https_proxy:-} \
        zypper --non-interactive --no-gpg-checks refresh
    sudo \
        http_proxy=${http_proxy:-} \
        https_proxy=${https_proxy:-} \
        zypper --no-refresh install -y \
            globus-gridftp-server-control-devel \
            globus-xio-devel \
            globus-xio-gsi-driver-devel \
            globus-gfork-devel \
            globus-ftp-control-devel \
            globus-authz-devel \
            globus-io-devel

    GRIDFTP_SERVER_VER=13.28
    cd /tmp
    wget -nc \
      https://repo.gridcf.org/gct6/sources/globus_gridftp_server-${GRIDFTP_SERVER_VER}.tar.gz
    rm -rf globus_gridftp_server-${GRIDFTP_SERVER_VER}
    tar xzf globus_gridftp_server-${GRIDFTP_SERVER_VER}.tar.gz
    cd globus_gridftp_server-${GRIDFTP_SERVER_VER}

    ./configure
    make -j"$(nproc)"
    sudo make install
    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}

    echo /usr/local/lib | sudo tee \
      /etc/ld.so.conf.d/gridftp.conf >/dev/null
    sudo ldconfig

    if [ ! -e /usr/sbin/globus-gridftp-server ]; then
    sudo ln -s \
        /usr/local/sbin/globus-gridftp-server \
        /usr/sbin/globus-gridftp-server
    fi

    cd $WORKDIR

    if [ ! -f /usr/lib/systemd/system/globus-gridftp-server.service ]; then
        sudo tee /usr/lib/systemd/system/globus-gridftp-server.service > /dev/null <<'EOF'
[Unit]
Description=Globus GridFTP Server
After=network.target remote_fs.target

[Service]
Type=forking
PIDFile=/run/globus-gridftp-server.pid
ExecStartPre=/bin/sh -c "[ -f /etc/gridftp.conf ] || touch /etc/gridftp.conf"
ExecStartPre=/bin/sh -c "[ -d /etc/gridftp.d ] || mkdir -p /etc/gridftp.d"
ExecStart=/usr/local/sbin/globus-gridftp-server -S \
  -p 2811 \
  -c /etc/gridftp.conf -C /etc/gridftp.d \
  -pidfile /run/globus-gridftp-server.pid
ExecReload=/bin/kill -HUP $MAINPID

[Install]
WantedBy=multi-user.target
EOF
        sudo systemctl daemon-reload
    fi
}

setup_for_ubuntu() {
    sudo \
        http_proxy=${http_proxy:-} \
        https_proxy=${https_proxy:-} \
        apt-get update
    PACKAGES="libglobus-gridftp-server-dev globus-gridftp-server-progs"
    sudo \
        http_proxy=${http_proxy:-} \
        https_proxy=${https_proxy:-} \
        apt-get -y install $PACKAGES
}

create_pkg() {
    ./configure
    make dist
}

get_pkg_name() {
    ls gfarm-gridftp-dsi-*.tar.gz | sort | tail -1  # print newest
}

install_from_source() {
    # for gfarm.pc
    PKG_CONFIG_PATH=/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}
    export PKG_CONFIG_PATH

    create_pkg
    PKG=$(get_pkg_name)
    SRCDIR=${PKG%.tar.gz}
    [ -d "./${SRCDIR}" ] && rm -rf "./${SRCDIR}"
    tar xvf $PKG
    cd "$SRCDIR"
    ./configure --libdir=$(pkg-config --variable=libdir globus-gridftp-server)
    #make
    make CPPFLAGS='-DUSE_GLOBUS_GFS_CMD_TRNC -DUSE_GLOBUS_GFS_CMD_SITE_RDEL'
    sudo make install
}

install_from_rpm() {
    spec="${WORKDIR}/${NAME}.spec"

    create_pkg
    PKG=$(get_pkg_name)
    NAME_VER=${PKG%.tar.gz}
    SRPM_FILE="rpmbuild/SRPMS/${NAME_VER}-*.src.rpm"
    RPM_FILE="rpmbuild/RPMS/x86_64/${NAME_VER}-*.rpm"

    mkdir -p ~/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPEC,SPECS,SRPMS}
    mv $PKG ~/rpmbuild/SOURCES/
    cd ~
    rpmbuild -bs "${spec}"
    rpmbuild --rebuild ${SRPM_FILE}

    case $GFDOCKER_PRJ_NAME in
    opensuse-*)
        sudo rpm -ivh --nodeps --force ${RPM_FILE}
        ;;
    *)
        sudo rpm -ivh --force ${RPM_FILE}
        ;;
    esac

    save_package ${SRPM_FILE}
    save_package ${RPM_FILE}
}

enable_for_systemd() {
    CONF=/etc/gridftp.conf
    sudo touch "$CONF"
    sudo mkdir -p /etc/gridftp.d
    sudo sed -i -e '/load_dsi_module .*/d' "$CONF"
    echo "load_dsi_module gfarm" | sudo tee -a "$CONF" > /dev/null
    if [ -f /sbin/chkconfig ]; then
        sudo /sbin/chkconfig --level=35 globus-gridftp-server on
    fi
    sudo systemctl enable globus-gridftp-server
    sudo systemctl restart globus-gridftp-server
}

enable_gridftp_server() {
    enable_for_systemd
    # TODO enable_for_xinetd ?
}

cd $WORKDIR

case $GFDOCKER_PRJ_NAME in
    centos*-src|rockylinux*-src|almalinux*-src|fedora*-src)
        setup_for_centos
        install_from_source
        enable_gridftp_server
        ;;
    centos*-pkg|rockylinux*-pkg|almalinux*-pkg|fedora*-pkg)
        setup_for_centos
        install_from_rpm
        enable_gridftp_server
        ;;
    opensuse-src)
        setup_for_opensuse
        install_from_source
        enable_gridftp_server
        ;;
    opensuse-pkg)
        setup_for_opensuse
        install_from_rpm
        enable_gridftp_server
        ;;
    ubuntu*-*|debian*-*)
        setup_for_ubuntu
        install_from_source
        enable_gridftp_server
        ;;
    *) exit 1;;
esac
