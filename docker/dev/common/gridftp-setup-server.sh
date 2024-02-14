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
    sudo yum -y update
    sudo yum -y install $PACKAGES
}

setup_for_opensuse() {
    sudo zypper --non-interactive --no-gpg-checks refresh
    sudo zypper --no-refresh install -y $PACKAGES
}

setup_for_ubuntu() {
    sudo apt-get update
    PACKAGES="libglobus-gridftp-server-dev globus-gridftp-server-progs"
    sudo apt-get -y install $PACKAGES
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
    PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
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

    sudo rpm -ivh --force ${RPM_FILE}

    save_package ${SRPM_FILE}
    save_package ${RPM_FILE}
}

enable_for_systemd() {
    CONF=/etc/gridftp.conf
    sudo sed -i -e '/load_dsi_module .*/d' $CONF
    echo "load_dsi_module gfarm" | sudo tee -a $CONF > /dev/null
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
    centos*-src|rockylinux*-src|almalinux*-src)
        setup_for_centos
        install_from_source
        enable_gridftp_server
        ;;
    centos*-pkg|rockylinux*-pkg|almalinux*-pkg)
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
    ubuntu*-*)
        setup_for_ubuntu
        install_from_source
        enable_gridftp_server
        ;;
    *) exit 1;;
esac
