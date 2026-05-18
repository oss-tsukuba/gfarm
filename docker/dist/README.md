# Docker containers for Gfarm developers

## Prerequisite

- Install `docker compose` ([Ubuntu](https://docs.docker.com/engine/install/ubuntu/) | [CentOS](https://docs.docker.com/engine/install/centos/)) and `make`.

- To allow docker compose to run with user privileges, add $USER to the docker group by `sudo usermod -aG docker $USER`

- Clone Gfarm
```
   % git clone https://github.com/oss-tsukuba/gfarm.git
```

## Proxy configuration (optional)

If you are in a proxy environment, configure proxy-related
environment variables before pulling Docker images or executing
Docker-related commands such as `minica.sh`, `docker compose build`,
or `docker compose up`.

The following environment variables are required in a proxy environment.

`PROXY_HOST` and `PROXY_PORT` are used for the `jwt-server`
related Docker images, and in this case `jwt-server`,
`jwt-server2`, and `keycloak` must be included in
`no_proxy`/`NO_PROXY`.

Example:

    % export PROXY_HOST=proxy.example.com
    % export PROXY_PORT=8080

    % export http_proxy=http://${PROXY_HOST}:${PROXY_PORT}
    % export https_proxy=http://${PROXY_HOST}:${PROXY_PORT}
    % export HTTP_PROXY=http://${PROXY_HOST}:${PROXY_PORT}
    % export HTTPS_PROXY=http://${PROXY_HOST}:${PROXY_PORT}

    % export no_proxy=localhost,127.0.0.1,jwt-server,jwt-server2,keycloak
    % export NO_PROXY=localhost,127.0.0.1,jwt-server,jwt-server2,keycloak

If Docker daemon proxy configuration is required, create a systemd
drop-in file.

    % sudo mkdir -p /etc/systemd/system/docker.service.d

    % sudo vi /etc/systemd/system/docker.service.d/http-proxy.conf

Example:

    [Service]
    Environment="http_proxy=..."
    Environment="https_proxy=..."
    Environment="HTTP_PROXY=..."
    Environment="HTTPS_PROXY=..."
    Environment="no_proxy=localhost,127.0.0.1,jwt-server,jwt-server2,keycloak"
    Environment="NO_PROXY=localhost,127.0.0.1,jwt-server,jwt-server2,keycloak"

Reload systemd configuration and restart Docker.

    % sudo systemctl daemon-reload
    % sudo systemctl restart docker

If Docker client proxy configuration is required, create
`~/.docker/config.json`.

    % mkdir -p ~/.docker

    % vi ~/.docker/config.json

Example:

    {
      "proxies": {
        "default": {
          "httpProxy": "...",
          "httpsProxy": "...",
          "noProxy": "localhost,127.0.0.1,jwt-server,jwt-server2,keycloak"
        }
      }
    }


## Explore on virtual clusters by VS Code dev containers

This section is an option only for VS Code users.

- Open gfarm/ directory
- Open Terminal -> New Terminal
```
   % cd docker/dist
   % sh ./minica.sh
```
- Install VS Code and Dev Containers extension
- Open a command palette by Ctrl+Shift+p and execute "Dev Containers: Rebulid and Reopen in Container"
- Open Terminal -> New Terminal
```
   % cd docker/dist
```
- follow the instructions below after "(in a container)"

## Explore on virtual clusters

    % cd gfarm/docker/dist
    % sh ./minica.sh
    % DIST=<distribution> docker compose build --build-arg UID=$(id -u) c1
    % DIST=<distribution> docker compose up -d
    ubuntu, rockylinux9, almalinux8, and centos7 are available as $DIST.  Default is ubuntu
    % make          # login to a container

    (in a container)
    % sh ./all.sh
    ./all-rpm.sh also available in case of rocylinux9, almalinux8 and centos7
    This will install and setup Gfarm.  Enjoy!
    % Ctrl-D

    % docker compose down -v --remove-orphans

If the gfarm source repository is not clean, link errors may happen during the `all.sh` execution.  In this case, clean the repository.

    % cd ~/gfarm
    % git clean -dxf

When you would like to execute `all.sh` (or `all-rpm.sh`) again, execute `unconfig.sh`.

When you change the source code, execute `docker/dist/install.sh` in the top source directory and `restart.sh`.

When you install Gfarm by `all.sh`, `regress.sh` and `failover.sh` are available for tests.

## For OAuth authentication

    % cd jwt-server
    (To update images if necessary)% make build
    % docker compose up -d
    % make setup
    % cd ..

- connect remote desktop to localhost:13389
- login ubuntu/ubuntu
- launch Firefox (or Chromium (/rdesktop/chromium-start.sh))
- open a terminal
- execute `/rdesktop/install-ca-for-browser.sh`
- close Firefox
- launch Firefox
- connect to https://jwt-server/ by Firefox
- (Automatically redirect to http://keycloak:8443/)
- login user1/PASSWORD
- click "Generate and Store a JSON Web Token" button
- user name and passphrase are displayed

```
(in a host)
% make          # login to a container
(in a container at docker/dist/)
% jwt-agent -s https://jwt-server/ -l user1
Passphrase: (paste the passphrase displayed)
% gfuser -A $(id -un) SASL user1
% sh edconf.sh oauth2
```

### Use http proxy instead of remote desktop

- proxy server settings for any web browser on your desktop
  - protocol: HTTP
  - server: (IP address of the host where Docker containers for Gfarm is running)
  - port: 13128
- connect to https://jwt-server/ by web browser
- ignore the certificate warning on https://jwt-server/
- ignore the certificate warning on https://keycloak/

## How to update keys in minica/ if those expires

```
(in a host)
% sh ./minica.sh --update
% cd jwt-server
% docker compose down -v --remove-orphans
% docker compose up -d
% make setup
% cd ..
```

## For HPCI Storage

    execute all.sh or all-rpm.sh
    % sh ./hpci.sh
    % mv ~/.globus ~/.globus.bak
    % export GFARM_CONFIG_FILE=$HOME/.gfarm2rc.hpci
    % jwt-agent -s https://elpis.hpci.nii.ac.jp/ -l HPCI_ID

To execute gfperf, execute the following after editing at least $PROJ and $HPCI\_ID

    % myproxy-logon -s portal.hpci.nii.ac.jp -t 168 -l HPCI-ID
    % sh gfperf.sh

## Batch tests

Build, install and setup tests for all distributions.

    % sh ./batchtest.sh [ regress | regress_full ]

When regress or regress\_full option is specified this test includes regression tests.  For all authentications, use regress\_full not regress.  The log files of regression tests are stored in gfarm/build/regress.

`batchtest.sh` accepts the following options.

- ubuntu, rockylinux9, almalinux8, centos7 - specifies a distrubution to test.  mutiple distributions can be specified

## Sanitizer test

please remove the following tests temporarily from regress/schedule,
because gfpcopy and db_journal_test currently have known bugs:

- gftool/gfprep/gfpcopy_dir_by_gfcp.sh
- gftool/gfprep/gfpcopy_file_by_gfcp.sh
- server/gfmd/db_journal/db_journal_write.sh
- server/gfmd/db_journal/db_journal_apply.sh

### ASAN/LSAN/UBSAN test

    (in a container)
    % rm -rf ~/gfarm/build-x86_64-* ~/gfarm/*/build-x86_64-*
    % sh ./all.sh asan
    % sh ./regress.sh
    % sh ./restart.sh
    ... and check /var/tmp/gfarm.log.{a,l,ub}san.* on host c{1..8}

NOTE:

- please ignore /var/tmp/gfarm.log.lsan.gfsd.*,
  because LSAN does NOT work with gfsd due to ptrace(2) isssue.
  instead, please use valgrind for gfsd.

### TSAN test

    (in a host)
    % sudo sysctl -w kernel.randomize_va_space=0

    (in a container)
    % rm -rf ~/gfarm/build-x86_64-* ~/gfarm/*/build-x86_64-*
    % sh ./all.sh tsan
    % sh ./regress.sh
    % sh ./restart.sh
    ... and check /var/tmp/gfarm.log.tsan.* on host c{1..8}

## Create RPM packages

    % sh ./devrpm.sh
    (in the container)
    # sh gfarm/docker/dist/mkrpm.sh gfarm 2.8.0
    # rpm -Uvh rpmbuild/RPMS/x86_64/gfarm-gsi-*
    # sh gfarm/docker/dist/mkrpm.sh gfarm2fs 1.2.19
    # Ctrl-D

    % export PKG=gfarm
    % export VER=2.8.0
    % docker cp alma8:/root/rpmbuild/SRPMS/$PKG-$VER-1.src.rpm .
    % docker stop alma8
    % docker rm alma8
