# Docker containers for Gfarm developers

## Prerequisite

    % git clone https://github.com/oss-tsukuba/gfarm.git
    % cd gfarm
    % git clone https://github.com/oss-tsukuba/gfarm2fs.git

## Explore on virtual clusters by VS Code dev containers

- Install VS Code and Remote Containers extension
- Open this directory
- Open a command palette by Ctrl+Shift+p and execute "Remote-Containers: Rebulid and Reopen in Container"
- Open Terminal -> New Terminal
- follow the instructions below in a container

## Explore on virtual clusters

    % DIST=<distribution> docker compose up -d
    ubuntu, almalinux8, and centos7 are available.  Default is ubuntu
    % docker exec -it gfarm-c1 sudo -u $USER sh -c "(cd ~/gfarm && /bin/bash)"

    (in a container)
    % cd docker/dist
    % sh ./all.sh
    or ./all-rpm.sh also available in case of almalinux8 and centos7
    This will install and setup Gfarm.  Enjoy!
    % Ctrl-D

    % docker compose down

If you would like to execute `all.sh` (or `all-rpm.sh`) again, execute `unconfig.sh`.

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
    # sh gfarm/docker/dist/mkrpm.sh
    # Ctrl-D

    % docker cp alma8:/root/rpmbuild/SRPMS/gfarm-$VER-1.src.rpm .
    % docker stop alma8
    % docker rm alma8
