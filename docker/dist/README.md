# Docker containers for Gfarm developers

## Prerequisite

    % git clone https://github.com/oss-tsukuba/gfarm.git
    % cd gfarm
    % git clone https://github.com/oss-tsukuba/gfarm2fs.git
    % cd docker/dist

## (For VS Code users) Explore on virtual clusters by VS Code dev containers

- Install VS Code and Dev Containers extension
- Open this directory
- Open a command palette by Ctrl+Shift+p and execute "Dev Containers: Rebulid and Reopen in Container"
- Open Terminal -> New Terminal
- follow the instructions below in a container

## Explore on virtual clusters

    % cd docker/dist
    % DIST=<distribution> docker compose build --build-arg UID=$(id -u) c1
    % DIST=<distribution> docker compose up -d
    ubuntu, almalinux8, and centos7 are available as $DIST.  Default is ubuntu
    % docker exec -u $USER -w /home/$USER/gfarm -it gfarm-c1 /bin/bash

    (in a container)
    % cd docker/dist
    % sh ./all.sh
    ./all-rpm.sh also available in case of almalinux8 and centos7
    This will install and setup Gfarm.  Enjoy!
    % Ctrl-D

    % docker compose down

When you would like to execute `all.sh` (or `all-rpm.sh`) again, execute `unconfig.sh`.

When you change the source code, execute `docker/dist/install.sh -m` in the top source directory and `restart.sh`.

When you install Gfarm by `all.sh`, `regress.sh` and `failover.sh` are available for tests.

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
    # wget https://github.com/oss-tsukuba/gfarm/archive/refs/tags/$VER.tar.gz
    # mv $VER.tar.gz $PKG-$VER.tar.gz
    # sh gfarm/docker/dist/mkrpm.sh
    # Ctrl-D

    % docker cp alma8:/root/rpmbuild/SRPMS/gfarm-$VER-1.src.rpm .
    % docker stop alma8
    % docker rm alma8
