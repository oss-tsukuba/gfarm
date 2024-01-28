# Docker containers for Gfarm developers in a heterogeneous environment

## Explore on virtual clusters

Install docker compose ([Ubuntu](https://docs.docker.com/engine/install/ubuntu/) | [CentOS](https://docs.docker.com/engine/install/centos/)) and make.

To allow docker compose to run with user privileges, add $USER to the docker group by `sudo usermod -aG docker $USER`

    % cd gfarm/docker/dist/mixed
    % docker compose up -d
    % make          # login to a container

    (in a container)
    % sh ./all.sh
    This will install and setup Gfarm.  Enjoy!
    % Ctrl-D

    % docker compose down

## Batch tests

Build, install and setup tests.

    % sh ./batchtest.sh
