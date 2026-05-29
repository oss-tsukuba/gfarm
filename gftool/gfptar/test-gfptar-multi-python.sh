#!/bin/bash
set -eu -o pipefail
#set -x

# This script uses Pyenv to run gfptar tests on multiple Python versions.
# The necessary packages for Pyenv, Pyenv itself, and each Python version
# will be automatically installed to ~/.local/pyenv .

# (MEMO) Default Python version for each distribution:
#   Ubuntu 26.04 : 3.14
#   Ubuntu 24.04 : 3.12
#   Ubuntu 22.04 : 3.10
#   Ubuntu 20.04 : 3.8
#   Debian 12 : 3.11
#   Debian 11 : 3.9
#   RHEL 10 : 3.12 / 3.13
#   RHEL  9 : 3.9
#   RHEL  8 : 3.6

TARGET_VERSIONS="
3.6.15
#3.8.20
3.9.25
3.12.13
#3.13.13
#3.13.13t
3.14.5
"
TESTDIR="${HOME}/test-gfptar-pyenv"
GFPTAR="${GFPTAR:-${HOME}/gfarm/gftool/gfptar/gfptar}"

print_func_name() {
    echo "*** $1 ***"
}

install_packages_for_debian() {
    print_func_name "install_packages_for_debian"
    sudo apt install -y \
         build-essential \
         zlib1g-dev libbz2-dev libffi-dev libreadline-dev \
         liblzma-dev libncurses-dev libsqlite3-dev libssl-dev
}

install_packages_for_rhel() {
    print_func_name "install_packages_for_rhel"
    sudo dnf install -y \
         libffi-devel gcc gcc-c++ zlib zlib-devel \
         readline-devel bzip2-devel ncurses-devel \
         sqlite-devel xz-devel
}

install_packages() {
    eval "$(grep '^ID_LIKE=' /etc/os-release)"

    for id in $ID_LIKE; do  # ID_LIKE from /etc/os-release
        case $id in
            debian)
                install_packages_for_debian
                break
                ;;
            rhel)
                install_packages_for_rhel
                break
                ;;
        esac
    done
}

install_pyenv() {
    print_func_name "install_pyenv"
    if [ -e ~/.local/pyenv ]; then
        echo "pyenv is already installed."
        return 0  # skip
    fi
    mkdir -p ~/.local ~/env
    git clone https://github.com/yyuu/pyenv.git ~/.local/pyenv
    cat <<EOF > ~/env/pyenv.sh
export PYENV_ROOT="\${HOME}/.local/pyenv"
export PATH="\${PYENV_ROOT}/bin:${PATH}"
eval "\$(pyenv init -)"
EOF
}

test_gfptar() {
    print_func_name "test_gfptar"
    mkdir -p "${TESTDIR}"
    cd "${TESTDIR}"
    ABS_PATH=$(pwd)
    for ver in ${TARGET_VERSIONS}; do
        if [[ "${ver}" =~ ^# ]]; then
            continue
        fi
        cd "${ABS_PATH}"
        mkdir -p "${ver}"
        cd "${ver}"
        pyenv install --skip-existing "${ver}"
        pyenv local "${ver}"
        pip3 install -q --upgrade pip
        pip3 install -q docopt schema
    done
    for ver in ${TARGET_VERSIONS}; do
        if [[ "${ver}" =~ ^# ]]; then
            continue
        fi
        cd "${ABS_PATH}"
        cd "${ver}"
        VER_REAL=$(python3 --version)
        echo -n "Running gfptar --test on ${VER_REAL} ... "
        start=$(($(date +%s%N) / 1000000))  # msec
        if "${GFPTAR}" --test -q > /dev/null 2>&1; then
            end=$(($(date +%s%N) / 1000000))  # msec
            diff=$((end - start))  # msec
            diff_sec=$(printf "%d.%03d" $((diff / 1000)) $((diff % 1000)))
            echo "PASS (${diff_sec} sec.)"
        else
            echo "FAIL"
        fi
    done
}

print_uninstall_guide() {
    print_func_name "print_uninstall_guide"
    cat <<EOF
Please delete the following directories:
  - ${HOME}/.local/pyenv
  - ${HOME}/env/pyenv.sh
  - ${TESTDIR}
EOF
}

install_packages
install_pyenv
# shellcheck source=/dev/null
. ~/env/pyenv.sh
test_gfptar
print_uninstall_guide
