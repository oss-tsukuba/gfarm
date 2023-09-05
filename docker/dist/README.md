# Docker containers for Gfarm developers

## Prerequisite

    % git clone https://github.com/oss-tsukuba/gfarm.git -b 2.8

## (For VS Code users) Explore on virtual clusters by VS Code dev containers

- Install VS Code and Dev Containers extension
- Open gfarm/ directory
- Open a command palette by Ctrl+Shift+p and execute "Dev Containers: Rebulid and Reopen in Container"
- Open Terminal -> New Terminal

      % cd docker/dist

- follow the instructions below after "in a container"

## Explore on virtual clusters

    % cd gfarm/docker/dist
    % DIST=<distribution> docker compose build --build-arg UID=$(id -u) c1
    % DIST=<distribution> docker compose up -d
    ubuntu, almalinux8, and centos7 are available as $DIST.  Default is ubuntu
    % make setup	# only required for OAuth Authentication
    % make

    (in a container)
    % sh ./all.sh
    ./all-rpm.sh also available in case of almalinux8 and centos7
    This will install and setup Gfarm.  Enjoy!
    % Ctrl-D

    % docker compose down

When you would like to execute `all.sh` (or `all-rpm.sh`) again, execute `unconfig.sh`.

When you change the source code, execute `docker/dist/install.sh -m` in the top source directory and `restart.sh`.

When you install Gfarm by `all.sh`, `regress.sh` and `failover.sh` are available for tests.

## For OAuth authentication

- connect remote desktop to localhost:13389
- login ubuntu/ubuntu
- launch Firefox
- open a terminal

      % /rdesktop/install-ca-for-browser.sh

- connect to jwt-server/ by Firefox
- login user1/PASSWORD
- click "Generate and Store a JSON Web Token" button
- user name and passphrase are displayed

      (in a host)
      % make
      (in a container at docker/dist/)
      % jwt-agent -s http://jwt-server/ -l user1
      Passphrase: (paste the passphrase displayed)
      % gfuser -A $USER SASL user1
      % sh edconf.sh oauth2

## For HPCI Storage

    execute all.sh or all-rpm.sh
    % sh ./hpci.sh
    % mv ~/.globus ~/.globus.bak
    % myproxy-logon -s portal.hpci.nii.ac.jp -t 168 -l HPCI-ID
    % export GFARM_CONFIG_FILE=$HOME/.gfarm2rc.hpci

## Batch tests

Build, install and setup tests for all distributions.

    % sh ./batchtest.sh

## Create RPM packages

    % sh ./devrpm.sh
    (in the container)
    # export PKG=gfarm
    # export VER=2.8.0
    # wget https://github.com/oss-tsukuba/$PKG/archive/refs/tags/$VER.tar.gz
    # mv $VER.tar.gz $PKG-$VER.tar.gz
    # sh gfarm/docker/dist/mkrpm.sh
    # Ctrl-D

    # export PKG=gfarm
    # export VER=2.8.0
    % docker cp alma8:/root/rpmbuild/SRPMS/$PKG-$VER-1.src.rpm .
    % docker stop alma8
    % docker rm alma8
