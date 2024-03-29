#!/bin/sh

set -eux

# References:
# https://github.com/multiarch/qemu-user-static
# https://www.kernel.org/doc/html/latest/admin-guide/binfmt-misc.html

SWITCH="$1"

docker buildx ls

case $SWITCH in
    check)
        if docker buildx ls | grep linux/arm64; then
            echo "enabled"
            exit 0
        else
            echo "Please setup qemu-user-static and binfmt_misc:"
            echo "Usage: $0 enable"
            exit 1
        fi
        ;;
    enable)
        docker run --rm --privileged multiarch/qemu-user-static --reset --persistent yes --credential yes
        docker buildx ls
        ;;
esac
