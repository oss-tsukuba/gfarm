# Docker containers for Gfarm developers

## Prerequisite

    % git clone https://github.com/oss-tsukuba/gfarm.git
    % cd gfarm
    % git clone https://github.com/oss-tsukuba/gfarm2fs.git
    % cd docker/dist

## Create RPM packages

    % sh ./devrpm.sh
    (in the container)
    # cd
    # sh gfarm/docker/dist/mkrpm.sh
    # Ctrl-D

    % docker cp alma8:/root/rpmbuild/SRPMS/gfarm-$VER-1.src.rpm .
    % docker stop alma8
    % docker rm alma8

## Explore on virtual clusters

    % DIST=<distribution> docker compose build
    Ubuntu, AlmaLinux8, and CentOS7 are available.  Default is Ubuntu
    % docker compose up -d
    % ssh $(docker exec gfarm-c1 hostname -i)
    For AlmaLinux8, need to wait at least 10 seconds until ssh server is ready

    (in a container)
    % cd gfarm/docker/dist
    % sh ./all.sh
    or ./all-rpm.sh also available in case of AlmaLinux8 and CentOS7
    This will install and setup Gfarm.  Enjoy!
    % Ctrl-D

    % docker compose down

If you would like to execute `all.sh` (or `all-rpm.sh`) again, execute `unconfig.sh`.

## For HPCI Storage

    eforxecute all.sh or all-rpm.sh
    % sh ./hpci.sh
    % mv ~/.globus ~/.globus.bak
    % myproxy-logon -s portal.hpci.nii.ac.jp -t 168 -l HPCI-ID
    % export GFARM_CONFIG_FILE=$HOME/.gfarm2rc.hpci
