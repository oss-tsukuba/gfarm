# Docker containers for Gfarm developers

## Prerequisite

    % git clone https://github.com/oss-tsukuba/gfarm.git

## Explore on virtual clusters by VS Code dev containers

This section is an option only for VS Code users.

- Install VS Code and Dev Containers extension
- Open gfarm/ directory
- Open a command palette by Ctrl+Shift+p and execute "Dev Containers: Rebulid and Reopen in Container"
- Open Terminal -> New Terminal
```
   % cd docker/dist
```
- follow the instructions below after "in a container"

## Explore on virtual clusters

Install docker compose ([Ubuntu](https://docs.docker.com/engine/install/ubuntu/) | [CentOS](https://docs.docker.com/engine/install/centos/)) and make.

To allow docker compose to run with user privileges, add $USER to the docker group by `sudo usermod -aG docker $USER`

    % cd gfarm/docker/dist
    % DIST=<distribution> docker compose build --build-arg UID=$(id -u) c1
    % DIST=<distribution> docker compose up -d
    ubuntu, rockylinux9, almalinux8, and centos7 are available as $DIST.  Default is ubuntu
    % make          # login to a container

    (in a container)
    % sh ./all.sh
    ./all-rpm.sh also available in case of rocylinux9, almalinux8 and centos7
    This will install and setup Gfarm.  Enjoy!
    % Ctrl-D

    % docker compose down

When you would like to execute `all.sh` (or `all-rpm.sh`) again, execute `unconfig.sh`.

When you change the source code, execute `docker/dist/install.sh -m` in the top source directory and `restart.sh`.

When you install Gfarm by `all.sh`, `regress.sh` and `failover.sh` are available for tests.

## For OAuth authentication

    % cd jwt-server
    % docker compose up -d
    % make setup
    % cd ..

- connect remote desktop to localhost:13389
- login ubuntu/ubuntu
- launch Firefox
- open a terminal
- execute `/rdesktop/install-ca-for-browser.sh`
- connect to jwt-server/ by Firefox
- login user1/PASSWORD
- click "Generate and Store a JSON Web Token" button
- user name and passphrase are displayed

```
(in a host)
% make          # login to a container
(in a container at docker/dist/)
% jwt-agent -s http://jwt-server/ -l user1
Passphrase: (paste the passphrase displayed)
% gfuser -A $(id -un) SASL user1
% sh edconf.sh oauth2
```

## For HPCI Storage

    execute all.sh or all-rpm.sh
    % sh ./hpci.sh
    % mv ~/.globus ~/.globus.bak
    % myproxy-logon -s portal.hpci.nii.ac.jp -t 168 -l HPCI-ID
    % export GFARM_CONFIG_FILE=$HOME/.gfarm2rc.hpci

To execute gfperf, execute `gfperf.sh` after editing at least $PROJ and $HPCI\_ID

## Batch tests

Build, install and setup tests for all distributions.

    % sh ./batchtest.sh

or

    % sh ./batchtest.sh regress

batchtest.sh accepts the following options.

- jwt - includes oauth2 authentication but for the first time
- ubuntu, rockylinux9, almalinux8, centos7 - specifies a distrubution to test.  mutiple distributions can be specified

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
