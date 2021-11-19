set -o xtrace
set -o errexit
set -o nounset
set -o pipefail

GFDOCKER_GFARMS3_UPDATE_ONLY=${GFDOCKER_GFARMS3_UPDATE_ONLY:-0}

# user1 executes this script.
SUDO="sudo -E HOME=/root"
SUDO_USER2="sudo sudo -u user2"

case $GFDOCKER_PRJ_NAME in
     *-src) WITH_GFARM=/usr/local;;
     *-pkg) WITH_GFARM=/usr;;
     *) exit 1;;
esac

GFARM_S3_MINIO_WEB_SRC=/mnt/work/gfarm-s3-minio-web
GFARM_S3_MINIO_SRC=/mnt/work/gfarm-s3-minio

if [ ! -d $GFARM_S3_MINIO_WEB_SRC ]; then
    exit 1
fi

## choose parameters

### clear files after containers are stopped
WORKDIR=$HOME/tmp/work

PERSISTENT_DIR=/mnt
### temporary directory to cache files for next build
PERSISTENT_TMPDIR=$PERSISTENT_DIR/user1/tmp/work
MINIO_BUILDDIR=$PERSISTENT_TMPDIR

GFARM_S3_PREFIX=/usr/local

GFARM_S3_USERNAME=_gfarm_s3
GFARM_S3_GROUPNAME=${GFARM_S3_USERNAME}
GFARM_S3_HOMEDIR=/home/${GFARM_S3_USERNAME}
GFARM_S3_LOCALTMP=/mnt/minio_tmp
GFARM_S3_LOCALTMP_SIZE=1024
GFARM_S3_WEBUI_ADDR=127.0.0.1:8000
GFARM_S3_ROUTER_ADDR=127.0.0.1:8001

# from config.mk
FRONT_WEBSERVER=$GFDOCKER_GFARMS3_FRONT_WEBSERVER
SHARED_DIR=$GFDOCKER_GFARMS3_SHARED_DIR
MYPROXY_SERVER=$GFDOCKER_GFARMS3_MYPROXY_SERVER
USERS=$GFDOCKER_GFARMS3_USERS
CERT_DIR=$WORKDIR/cert

MY_HOSTNAME=$(hostname)
MY_IPADDR=$(dig $MY_HOSTNAME +short)

install_package_for_centos() {
    ${SUDO} yum update -y
    ${SUDO} yum install -y \
        uuid \
        myproxy \
        python3-devel \
        python3-pip \
        nodejs

    case $FRONT_WEBSERVER in
        apache)
            ${SUDO} yum install -y httpd mod_ssl
            ;;
        nginx)
            ${SUDO} yum install -y nginx
            ;;
        *)
            exit 1
            ;;
    esac
}

install_package_for_ubuntu() {
    ${SUDO} apt-get update
    ${SUDO} apt-get upgrade -y

    case $GFDOCKER_PRJ_NAME in
        ubuntu1804-*)
            # for npm:
            #   npm : Depends: node-gyp (>= 0.10.9) but it is not going
            #   to be installed
            ${SUDO} apt-get install -y \
                    nodejs-dev node-gyp libssl1.0-dev
            ;;
    esac
    # install old npm to install new npm
    ${SUDO} apt-get install -y npm
    # old version
    /usr/bin/npm --version || true  # may fail when re-installing
    # install new npm (LTS version) with n
    ${SUDO} npm install -g n
    ${SUDO} n lts
    # new version
    /usr/local/bin/npm --version
    # remove old version
    ${SUDO} apt-get remove -y nodejs npm
    ${SUDO} apt-get autoremove -y

    ${SUDO} apt-get install -y \
        uuid \
        myproxy \
        python3 \
        python3-pip \
        python3-dev

    # to download awscli
    ${SUDO} apt-get install -y curl

    case $FRONT_WEBSERVER in
        apache)
            ${SUDO} apt-get install -y apache2
            ;;
        nginx)
            ${SUDO} apt-get install -y nginx
            ;;
        *)
            exit 1
            ;;
    esac
}

install_package() {
    case $GFDOCKER_PRJ_NAME in
        centos*-*)
            install_package_for_centos
            ;;
        ubuntu*-*)
            install_package_for_ubuntu
            ;;
        *)
            exit 1
            ;;
    esac
}

install_prerequisites() {
    install_package

    ${SUDO} python3 -m pip install -q Django
    ${SUDO} python3 -m pip install -q gunicorn

    ## for test.py
    ${SUDO} python3 -m pip install -q boto3
}

create_certificate() {
    mkdir -p $CERT_DIR
    cd $CERT_DIR
    openssl genrsa 2048 > ca.key
    openssl rsa -in ca.key -pubout -out ca.pub
    openssl req -new -key ca.key -subj "/CN=testca" > ca.csr
    openssl x509 -req -in ca.csr -signkey ca.key -days 1825 -out ca.crt

    echo "subjectAltName = DNS:${MY_HOSTNAME}, IP:${MY_IPADDR}" > san.txt
    openssl genrsa 2048 > server.key
    openssl req -new -key server.key -subj "/CN=${MY_HOSTNAME}" > server.csr
    openssl x509 -req -in server.csr -out server.crt -days 365 -CAkey ca.key -CA ca.crt -CAcreateserial -extfile san.txt

    case $GFDOCKER_PRJ_NAME in
        centos*-*)
            SYSTEM_CERT_DIR=/usr/share/pki/ca-trust-source/anchors
            GFARM_CA_CERT_DIR=$SYSTEM_CERT_DIR
            SYSTEM_CERT_UPDATE=update-ca-trust
            SERVER_CERT_DIR=/etc/pki/tls/certs
            SERVER_KEY_DIR=/etc/pki/tls/private
            ;;
        ubuntu*-*)
            SYSTEM_CERT_DIR=/usr/local/share/ca-certificates
            GFARM_CA_CERT_DIR=$SYSTEM_CERT_DIR/gfarm
            SYSTEM_CERT_UPDATE=update-ca-certificates
            SERVER_CERT_DIR=/etc/ssl/server
            SERVER_KEY_DIR=/etc/ssl/server
            ${SUDO} mkdir -p $SERVER_CERT_DIR
            ${SUDO} chmod 700 $SERVER_CERT_DIR
            ;;
        *)
            exit 1
            ;;
    esac

    GFARM_CA_CERT=$GFARM_CA_CERT_DIR/ca.crt
    SERVER_CERT=$SERVER_CERT_DIR/server.crt
    SERVER_KEY=$SERVER_KEY_DIR/server.key

    if [ ! -f $GFARM_CA_CERT ]; then
        ${SUDO} mkdir -p $GFARM_CA_CERT_DIR
        ${SUDO} cp ca.crt $GFARM_CA_CERT_DIR
        ${SUDO} chown root:root $GFARM_CA_CERT

        ${SUDO} $SYSTEM_CERT_UPDATE

        ${SUDO} cp server.crt $SERVER_CERT
        ${SUDO} chown root:root $SERVER_CERT
        ${SUDO} chmod 644 $SERVER_CERT

        ${SUDO} cp server.key $SERVER_KEY
        ${SUDO} chown root:root $SERVER_KEY
        ${SUDO} chmod 600 $SERVER_KEY
    fi
}

deploy_apache_for_centos() {
    cat <<'EOF' \
        | sed -e "s;@SERVER_CERT@;$SERVER_CERT;" \
              -e "s;@SERVER_KEY@;$SERVER_KEY;" \
              -e "s;@HTTPD_COMMON_CONF@;$HTTPD_COMMON_CONF;" \
              -e "s;@MY_HOSTNAME@;$MY_HOSTNAME;" \
        | ${SUDO} dd of="$HTTPD_CONF"
ServerName @MY_HOSTNAME@
<VirtualHost *:80>
	Include @HTTPD_COMMON_CONF@
</VirtualHost>

<VirtualHost *:443>
	SSLEngine on
	SSLCertificateFile @SERVER_CERT@
	SSLCertificateKeyFile @SERVER_KEY@
	Include @HTTPD_COMMON_CONF@
</VirtualHost>
EOF

    cat <<EOF | ${SUDO} dd of=$HTTPD_COMMON_CONF
DocumentRoot $HTTPD_DocumentRoot
ServerAdmin root@localhost
CustomLog logs/access_log common
ErrorLog logs/error_log

<Directory "$HTTPD_DocumentRoot">
	AllowOverride FileInfo AuthConfig Limit Indexes
	Options MultiViews Indexes SymLinksIfOwnerMatch Includes
	AllowOverride All
	Require all granted
</Directory>
EOF

    ## for debug (do not apply following settings on service host)
    ${SUDO} chmod og+rX /var/log/httpd
}

deploy_apache_for_ubuntu() {
    cat <<'EOF' \
        | sed -e "s;@SERVER_CERT@;$SERVER_CERT;" \
            -e "s;@SERVER_KEY@;$SERVER_KEY;" \
            -e "s;@HTTPD_COMMON_CONF@;$HTTPD_COMMON_CONF;" \
            -e "s;@MY_HOSTNAME@;$MY_HOSTNAME;" \
        | ${SUDO} dd of=$HTTPD_CONF
ServerName @MY_HOSTNAME@
<VirtualHost *:80>
	Include @HTTPD_COMMON_CONF@
</VirtualHost>

<VirtualHost *:443>
	SSLEngine on
	SSLCertificateFile @SERVER_CERT@
	SSLCertificateKeyFile @SERVER_KEY@
	Include @HTTPD_COMMON_CONF@
</VirtualHost>
EOF

    cat <<EOF | ${SUDO} dd of="$HTTPD_COMMON_CONF"
DocumentRoot $HTTPD_DocumentRoot
ServerAdmin root@localhost
CustomLog /var/log/apache2/access_log common
ErrorLog /var/log/apache2/error_log

<Directory "$HTTPD_DocumentRoot">
	AllowOverride FileInfo AuthConfig Limit Indexes
	Options MultiViews Indexes SymLinksIfOwnerMatch Includes
	AllowOverride All
	Require all granted
</Directory>
EOF

    HTTPD_UNIT_DROPIN=/etc/systemd/system/apache2.d
    HTTPD_COMMON_ENV=$HTTPD_UNIT_DROPIN/env.conf
    ${SUDO} mkdir -p $HTTPD_UNIT_DROPIN
    ${SUDO} cat <<EOF | ${SUDO} dd of=$HTTPD_COMMON_ENV
Environment="APACHE_RUN_USER=www-data APACHE_RUN_GROUP=www-data APACHE_PID_FILE=/var/run/apache2/apache2.pid APACHE_RUN_DIR=/var/run/apache2 APACHE_LOCK_DIR=/var/lock/apache2 APACHE_LOG_DIR=/var/log/apache2 LANG=C"
EOF
    ${SUDO} systemctl daemon-reload

    ${SUDO} a2ensite `basename $HTTPD_CONF`
    ${SUDO} a2dissite 000-default
    ${SUDO} a2enmod ssl
    ${SUDO} a2enmod rewrite
    ${SUDO} a2enmod proxy
    ${SUDO} a2enmod proxy_http
    ${SUDO} systemctl restart $HTTPD_UNIT_NAME
}

deploy_apache() {
    case $GFDOCKER_PRJ_NAME in
        centos*-*)
            HTTPD_UNIT_NAME=httpd
            HTTPD_CONF=/etc/httpd/conf.d/myserver.conf
            HTTPD_COMMON_CONF=/etc/httpd/conf.d/myserver-common.conf
            deploy_apache_for_centos
            ;;
        ubuntu*-*)
            HTTPD_UNIT_NAME=apache2
            HTTPD_CONF=/etc/apache2/sites-available/myserver.conf
            HTTPD_COMMON_CONF=/etc/apache2/sites-available/myserver-common.conf
            deploy_apache_for_ubuntu
            ;;
        *)
            exit 1
            ;;
    esac

    ${SUDO} systemctl enable $HTTPD_UNIT_NAME
}

gen_nginx_conf() {
    CONF="$1"

    cat <<'EOF' \
        | sed -e "s;@HTTPD_DocumentRoot@;$HTTPD_DocumentRoot;" \
              -e "s;@SERVER_CERT@;$SERVER_CERT;" \
              -e "s;@SERVER_KEY@;$SERVER_KEY;" \
              -e "s;@MY_HOSTNAME@;$MY_HOSTNAME;" \
	      -e "s;@GFARM_S3_HOMEDIR@;$GFARM_S3_HOMEDIR;" \
        | ${SUDO} dd of="$CONF"
server {
  listen 80;
  listen [::]:80;

  listen 443 ssl;
  listen [::]:443 ssl;

  ssl_certificate @SERVER_CERT@;
  ssl_certificate_key @SERVER_KEY@;

  root @HTTPD_DocumentRoot@;
  index index.html index.htm;

  server_name @MY_HOSTNAME@;

  client_max_body_size 16m;

  location / {
    proxy_set_header Host $http_host;
    proxy_set_header X-Forwarded-Host $host:$server_port;
    proxy_set_header X-Forwarded-Server $host;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;

    if ($http_authorization ~ "AWS4-HMAC-SHA256.*") {
      proxy_pass http://127.0.0.1:8001;
      break;
    }
  }

  location /gfarm/ {
    proxy_pass http://127.0.0.1:8000;
  }

  location /static/ {
    alias @GFARM_S3_HOMEDIR@/static/;
    autoindex off;
  }
}
EOF
}

deploy_nginx_for_centos() {
    NGINX_GFARM_CONF=/etc/nginx/conf.d/gfarm.conf

    gen_nginx_conf "$NGINX_GFARM_CONF"
}

deploy_nginx_for_ubuntu() {
    NGINX_SITE_AVAILABLE=/etc/nginx/sites-available
    NGINX_SITE_ENABLED=/etc/nginx/sites-enabled
    NGINX_GFARM_AVAILABLE="$NGINX_SITE_AVAILABLE/gfarm"
    NGINX_GFARM_ENABLED="$NGINX_SITE_ENABLED/gfarm"
    NGINX_DEFAULT_ENABLED="$NGINX_SITE_ENABLED/default"

    gen_nginx_conf "$NGINX_GFARM_AVAILABLE"

    ${SUDO} ln -f -s "$NGINX_GFARM_AVAILABLE" "$NGINX_GFARM_ENABLED"
    ${SUDO} rm -f "$NGINX_DEFAULT_ENABLED"
}

deploy_nginx() {
    case $GFDOCKER_PRJ_NAME in
        centos*-*)
            deploy_nginx_for_centos
            ;;
        ubuntu*-*)
            deploy_nginx_for_ubuntu
            ;;
        *)
            exit 1
            ;;
    esac
}

deploy_http_server() {
    #HTTPD_DocumentRoot=/usr/local/share/www
    HTTPD_DocumentRoot=/var/www/html
    INDEX_HTML="${HTTPD_DocumentRoot}/index.html"

    case $FRONT_WEBSERVER in
        apache)
            deploy_apache
            ;;
        nginx)
            deploy_nginx
            ;;
        *)
            exit 1
            ;;
    esac

    if [ ! -f "$INDEX_HTML" ]; then
        ${SUDO} mkdir -p "$HTTPD_DocumentRoot"
        echo '<a href="/gfarm/console">Gfarm-S3</a>' |
                ${SUDO} dd of="$INDEX_HTML"
    fi
}

install_gfarm_s3() {
    cd $WORKDIR/gfarm-s3-minio-web
    ./configure \
        --prefix=$GFARM_S3_PREFIX \
        --with-gfarm=$WITH_GFARM \
        --with-globus=/usr \
        --with-myproxy=/usr \
        --with-apache=/usr \
        --with-gunicorn=/usr/local \
        --with-gfarm-s3-homedir=$GFARM_S3_HOMEDIR \
        --with-gfarm-s3-user=$GFARM_S3_USERNAME \
        --with-gfarm-s3-group=$GFARM_S3_GROUPNAME \
        --with-webui-addr=$GFARM_S3_WEBUI_ADDR \
        --with-router-addr=$GFARM_S3_ROUTER_ADDR \
        --with-cache-basedir=$GFARM_S3_LOCALTMP \
        --with-cache-size=$GFARM_S3_LOCALTMP_SIZE \
        --with-myproxy-server=$MYPROXY_SERVER \
        --with-gfarm-shared-dir=$SHARED_DIR \
        --with-minio-builddir=$MINIO_BUILDDIR
    make
    ${SUDO} make install
    cd -
}

setup_apache() {
    ## edit HTTPD's configfile (i.e. myserver-common.conf)
    tmpfile=$(mktemp /tmp/XXXXXX) || exit 1
    (cat $HTTPD_COMMON_CONF
    cat $WORKDIR/gfarm-s3-minio-web/etc/apache-gfarm-s3.conf
    ) >$tmpfile
    ${SUDO} cp $WORKDIR/gfarm-s3-minio-web/etc/e403.html $HTTPD_DocumentRoot/e403.html
    ${SUDO} chown --reference=$HTTPD_COMMON_CONF $tmpfile
    ${SUDO} chgrp --reference=$HTTPD_COMMON_CONF $tmpfile
    ${SUDO} chmod --reference=$HTTPD_COMMON_CONF $tmpfile
    ${SUDO} mv $tmpfile $HTTPD_COMMON_CONF
    ${SUDO} systemctl restart $HTTPD_UNIT_NAME
}

setup_nginx() {
    ${SUDO} systemctl restart nginx
}

configure_gfarm_s3() {
    ## create cache directory for S3 multipart
    ${SUDO} mkdir -p $GFARM_S3_LOCALTMP
    ${SUDO} chmod 1777 $GFARM_S3_LOCALTMP

    ## create shared directory on Gfarm
    gfmkdir -p ${SHARED_DIR#/}
    gfchmod 0755 ${SHARED_DIR#/}

    . ${GFARM_S3_PREFIX}/etc/gfarm-s3.conf

    ${SUDO} truncate --size=0 "${GFARMS3_LOCAL_USER_MAP}"
    ## register users
    for u in $USERS; do
        # match whole line
        # if grep -q -x ${u} ${GFARMS3_LOCAL_USER_MAP}; then
        #     echo "already exists: ${u}"
        #     continue
        # fi
        global_user=${u%%:*}
        local_user=${u%:*}
        local_user=${local_user#*:}
        access_key_id=${u##*:}
        ${SUDO} $GFARM_S3_PREFIX/bin/gfarm-s3-useradd $global_user $local_user $access_key_id
        ${SUDO} usermod -a -G ${GFARM_S3_GROUPNAME} $local_user
        shared_dir_user=${SHARED_DIR#/}/$local_user
        gfsudo gfmkdir -p $shared_dir_user
        gfsudo gfchmod 0755 $shared_dir_user
        gfsudo gfchown $global_user $shared_dir_user
    done

    case $FRONT_WEBSERVER in
        apache)
            setup_apache
            ;;
        nginx)
            setup_nginx
            ;;
        *)
            exit 1
            ;;
    esac

    ## start gfarm-s3 WebUI service (gunicorn)
    ${SUDO} systemctl disable --now gfarm-s3-webui.service
    ${SUDO} systemctl enable --now gfarm-s3-webui.service
    ${SUDO} systemctl restart --now gfarm-s3-webui.service

    ## start gfarm-s3 MinIO router service (gunicorn)
    ${SUDO} systemctl disable --now gfarm-s3-router.service
    ${SUDO} systemctl enable --now gfarm-s3-router.service
    ${SUDO} systemctl restart --now gfarm-s3-router.service
}

install_aws_cli() {
    ## install AWS CLI v1
    #sudo yum install -y awscli

    # install AWS CLI v2
    AWSCLI_URL="https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip"
    #AWSCLI_URL="https://awscli.amazonaws.com/awscli-exe-linux-aarch64.zip"
    AWSCLI_ZIP=${PERSISTENT_TMPDIR}/awscliv2.zip

    if [ ! -f ${AWSCLI_ZIP} ]; then
        curl ${AWSCLI_URL} -o ${AWSCLI_ZIP}
    fi

    cd ${WORKDIR}
    # overwrite existing files
    unzip -q -o ${AWSCLI_ZIP}
    ${SUDO} ./aws/install --update

    /usr/local/bin/aws --version
}

install_s3cmd() {
    # XXX TODO define in Dockerfile
    ## install s3cmd
    case $GFDOCKER_PRJ_NAME in
        centos*-*)
            ${SUDO} yum install -y s3cmd
            ;;
        ubuntu*-*)
            ${SUDO} apt-get install -y s3cmd
            ;;
       *)
            ;;
    esac
}

install_goofys_dep_package_for_centos() {
    ${SUDO} yum install -y wget
}

install_goofys_dep_package_for_ubuntu() {
    ${SUDO} apt-get install -y wget
}

install_goofys() {
    case $GFDOCKER_PRJ_NAME in
        centos*-*)
            install_goofys_dep_package_for_centos
            ;;
        ubuntu*-*)
            install_goofys_dep_package_for_ubuntu
            ;;
       *)
            ;;
    esac

    ### use source package
    # GOPATH=${HOME}/work
    # export GOPATH
    # GO=${MINIO_BUILDDIR}/minio/work/go/bin/go
    # GOOFYS_URL=github.com/kahing/goofys
    # ${GO} get ${GOOFYS_URL}
    # ${GO} install ${GOOFYS_URL}

    ### use binary
    GOOFYS_URL=https://github.com/kahing/goofys/releases/latest/download/goofys
    GOOFYS_DOWNLOAD=${PERSISTENT_TMPDIR}/goofys
    GOOFYS_BIN=/usr/local/bin/goofys
    if [ ! -f ${GOOFYS_DOWNLOAD} ]; then
        ${SUDO} wget -O ${GOOFYS_DOWNLOAD} ${GOOFYS_URL}
    fi
    ${SUDO} cp -a ${GOOFYS_DOWNLOAD} ${GOOFYS_BIN}
    ${SUDO} chmod +x ${GOOFYS_BIN}
}

install_s3fs_dep_package_for_centos() {
    ${SUDO} yum install -y \
        automake \
        gcc-c++ \
        fuse-devel \
        openssl-devel \
        libcurl-devel \
        libxml2-devel
}

install_s3fs_dep_package_for_ubuntu() {
    ${SUDO} apt-get install -y \
        automake \
        g++ \
        libfuse-dev \
        libssl-dev \
        libcurl4-openssl-dev \
        libxml2-dev
}

install_s3fs() {
    case $GFDOCKER_PRJ_NAME in
        centos*-*)
            install_s3fs_dep_package_for_centos
            ;;
        ubuntu*-*)
            install_s3fs_dep_package_for_ubuntu
            ;;
       *)
            ;;
    esac

    S3FS_URL=https://github.com/s3fs-fuse/s3fs-fuse.git
    [ -d $WORKDIR/s3fs-fuse ] || (cd $WORKDIR && git clone --depth 1 ${S3FS_URL})
    (cd $WORKDIR/s3fs-fuse && \
    ./autogen.sh && \
    ./configure --prefix=/usr && \
    make -j && \
    ${SUDO} make install)
}

#cleanup() {
#    (cd $WORKDIR/gfarm-s3-minio-web && ${SUDO} make clean)
#    (cd $WORKDIR/gfarm-s3-minio-web && ${SUDO} make distclean)
#}

#########################################################################
### main

## create working directory
mkdir -p $WORKDIR

# "make install-go" installs "golang" into $MINIO_BUILDDIR/minio/work/go
MINIO_BUILDDIR_WORKDIR=${MINIO_BUILDDIR}/minio/work/build
${SUDO} mkdir -p ${MINIO_BUILDDIR_WORKDIR}
${SUDO} chown -R user1 ${MINIO_BUILDDIR}

MINIO_WEB_WORK_SRCDIR=$WORKDIR/gfarm-s3-minio-web
MINIO_WORK_SRCDIR=$MINIO_BUILDDIR_WORKDIR/gfarm-s3-minio

basename_cmp() {
    A=$(basename "$1")
    B=$(basename "$2")
    test "$A" = "$B"
    return $?
}

safe_rsync() {
    FROM="$1"
    TO="$2"
    basename_cmp "$FROM" "$TO" || exit 1
    ${SUDO} rsync --delete -rlptD "$FROM" "$TO"
}

## obtain or update source code
safe_rsync "$GFARM_S3_MINIO_WEB_SRC/" "$MINIO_WEB_WORK_SRCDIR/"
if [ -e $GFARM_S3_MINIO_SRC ]; then
    safe_rsync "$GFARM_S3_MINIO_SRC/" "$MINIO_WORK_SRCDIR/"
fi
${SUDO} chown -R user1 $MINIO_WEB_WORK_SRCDIR/

## update only
if [ $GFDOCKER_GFARMS3_UPDATE_ONLY -eq 1 ]; then
    install_gfarm_s3
    exit 0
fi

### install prerequisites
install_prerequisites

### site settings

## create self signed certificate
create_certificate

## deploy http server
deploy_http_server

## create user for Gfarm S3 system
${SUDO} groupadd $GFARM_S3_GROUPNAME || true
id $GFARM_S3_USERNAME || \
    ${SUDO} useradd -m $GFARM_S3_USERNAME -g $GFARM_S3_GROUPNAME -d $GFARM_S3_HOMEDIR

## install gfarm-s3
install_gfarm_s3

## gfarm-s3-settings
configure_gfarm_s3

## install aws
install_aws_cli

## install s3cmd
install_s3cmd

## install goofys
install_goofys

## install s3fs
install_s3fs

## cleanup
# cleanup

update_secret() {
    user="${1}"
    secret="${2}"
    gfarm_s3_dir="/home/${user}/.gfarm-s3"
    if [ -n "${secret}" ]; then
        ${SUDO} mkdir -p "${gfarm_s3_dir}"
        secret_file="${gfarm_s3_dir}/secret_key"
        ${SUDO} touch "${secret_file}"
        ${SUDO} chmod 700 "${secret_file}"
        echo "${secret}" | ${SUDO} dd of="${secret_file}"
        ${SUDO} chown -R "${user}" "${gfarm_s3_dir}"
    fi
}

update_secret user1 "${GFDOCKER_GFARMS3_SECRET_USER1}"
update_secret user2 "${GFDOCKER_GFARMS3_SECRET_USER2}"


## show sharedsecret password
echo "WebUI password for user1:"
$GFARM_S3_PREFIX/bin/gfarm-s3-sharedsecret-password

echo "WebUI password for uesr2:"
$SUDO_USER2 $GFARM_S3_PREFIX/bin/gfarm-s3-sharedsecret-password
