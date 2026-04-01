# Gfarm Installation Manual

## Introduction

This document describes the installation procedure for Gfarm.  Gfarm consists of a metadata server, filesystem nodes, and clients, each of which must be installed on its respective host.

In the command examples provided in this document, a command prompt of `#` indicates execution as root, while a prompt of `$` indicates execution as a regular user.

## System Requirements

Various Unix/Linux-based operating systems are available, but this document assumes Rocky Linux 9 and Ubuntu 24.04.  For other versions or operating systems, please adapt the instructions accordingly.

## Installation and Configuration

### Network port setting

By default, connections to the metadata server (gfmd) use TCP port 601, and connections to the filesystem node (gfsd) use TCP port 600 and UDP port 600.  These ports must be accessible from outside for both the metadata server and the filesystem node.

If there are restrictions on outbound ports, open them to allow connections on the ports listed above.

### Gfarm Installation

#### Required packages

Update the OS and packages to the latest versions, and install the required packages for Gfarm.

The following is for Rocky Linux 9.

```console
# dnf -y upgrade
# dnf -y groupinstall 'Development Tools'
# dnf -y install epel-release
# dnf -y install libtool openssl-devel
# dnf -y install cyrus-sasl-devel scitokens-cpp-devel
# dnf -y install golang python3
# dnf -y install python3-docopt python3-schema
# dnf config-manager --set-enabled crb
# dnf -y install fuse fuse-devel libacl-devel
# dnf -y install jq wget git
# dnf -y install libpq-devel postgresql postgresql-server ruby
```

The following is for Ubuntu 24.04.

```console
# apt-get update
# apt-get -y upgrade
# apt-get -y install gcc make libtool pkgconf
# apt-get -y install libssl-dev
# apt-get -y install libsasl2-dev sasl2-bin libscitokens-dev
# apt-get -y install golang python3
# apt-get -y install python3-docopt python3-schema
# apt-get -y install fuse libfuse-dev libacl1-dev
# apt-get -y install jq wget curl git
# apt-get -y install python3-venv bzip2 xz-utils
# apt-get -y install libpq-dev postgresql postgresql-client ruby
```

#### Nscd

Install and configure the Name Service Cache Daemon (nscd) to run when the operating system starts up. Gfarm can still be used if nscd is not running, though performance may be impacted.

The following is for Rocky Linux 9.

```console
# dnf -y install nscd
```

The following is for Ubuntu 24.04.

```console
# apt-get -y install nscd
```

Enable nscd to run at OS startup, and start it.

```console
# systemctl enable nscd
# systemctl start nscd
```

#### Time synchronization

Since Gfarm cannot be accessed if the time is out of sync, the time is synchronized using NTP (Network Time Protocol) or similar.

#### CA Certificates

Install CA certificates `$CAHASH.0` in `/etc/pki/tls/certs/gfarm`.

#### Installing cyrus-sasl-xoauth2-idp

Download the cyrus-sasl-xoauth2-idp source code from the following URL:  
<https://github.com/oss-tsukuba/cyrus-sasl-xoauth2-idp/releases>

This document describes version 1.0.2, but if a newer version is available, use the latest version.

Compile as any user (does not need to be root), then install with the root privilege.

```console
$ wget --content-disposition \
  https://github.com/oss-tsukuba/cyrus-sasl-xoauth2-idp/archive/1.0.2.tar.gz
$ tar pxf cyrus-sasl-xoauth2-idp-1.0.2.tar.gz
$ cd cyrus-sasl-xoauth2-idp-1.0.2
$ ./autogen.sh
$ ./configure --libdir=$(pkg-config --variable=libdir libsasl2)
$ make
$ sudo make install
```

For metadata servers and filesystem nodes, set up the configuration file `gfarm.conf`.

```console
# vi $(pkg-config --variable=libdir libsasl2)/sasl2/gfarm.conf
```

```text
mech_list: xoauth2
xoauth2_scope: xxxx
xoauth2_aud: xxxx
xoauth2_user_claim: xxxx
xoauth2_issuers: xxxx
```

For client, set up the configuration file `gfarm-client.conf`.

```console
# vi $(pkg-config --variable=libdir libsasl2)/sasl2/gfarm-client.conf
```

```text
xoauth2_user_claim: xxxx
```

Set an appropriate value for `xxxx`.

#### Installing Gfarm

Download the Gfarm source code from the following URL:  
<https://github.com/oss-tsukuba/gfarm/releases>

This document describes version 2.8.7, but if a newer version is available, use the latest version.

Compile as any user (does not need to be root), then install with the root privilege.

```console
$ wget --content-disposition \
  https://github.com/oss-tsukuba/gfarm/archive/2.8.7.tar.gz
$ tar pxf gfarm-2.8.7.tar.gz
$ cd gfarm-2.8.7
$ ./configure --sysconfdir=/etc --enable-xmlattr --enable-tls13 --enable-cyrus-sasl
$ make
$ sudo make install
```

#### Installing gfarm2fs

gfarm2fs is required on the client.  Download the gfarm2fs source code from the following URL:  
<https://github.com/oss-tsukuba/gfarm2fs/releases>

This document describes version 1.2.22, but if a newer version is available, use the latest version.

Compile as any user (does not need to be root), then install with the root privilege.

```console
$ wget --content-disposition \
  https://github.com/oss-tsukuba/gfarm2fs/archive/1.2.22.tar.gz
$ tar pxf gfarm2fs-1.2.22.tar.gz
$ cd gfarm2fs-1.2.22
$ ./configure --with-gfarm=/usr/local
$ make
$ sudo make install
```

#### Installing jwt-agent

jwt-agent is required on the client.  Download the jwt-agent source code from the following URL:  
<https://github.com/oss-tsukuba/jwt-agent/releases>

This document describes version 1.1.0, but if a newer version is available, use the latest version.

Compile as any user (does not need to be root), then install with the root privilege.

```console
$ wget --content-disposition \
  https://github.com/oss-tsukuba/jwt-agent/archive/1.1.0.tar.gz
$ tar pxf jwt-agent-1.1.0.tar.gz
$ cd jwt-agent-1.1.0
$ make
$ sudo make PREFIX=/usr/local install
```

#### Configuring core dump file output

Configure the system to save core dump files related to Gfarm and the executable files at the time of the problem for a sufficient period of time to investigate the problem.

The following is an example of configuring systemd-coredump and abrtd services to stop and keep core dump files in `/var/tmp`.

##### Stopping core dump acquisition services

The following is for Rocky Linux 9.

```console
# systemctl mask systemd-coredump.socket
```

The following is for Ubuntu 24.04.

```console
# systemctl stop apport
# systemctl disable apport
```

##### Checking kernel parameters

Create the configuration file `/etc/sysctl.d/gfarm.conf` with the following contents.

```text
fs.suid_dumpable = 1
kernel.core_pattern = /var/tmp/core.%e.%p
```

The `kernel.core_pattern` line specifies the path to the core file. Here, `%e` and `%p` are expanded to the program name and process ID, respectively. After placing this file, execute the following to load the settings.

```console
# /sbin/sysctl -p /etc/sysctl.d/gfarm.conf
```

##### System-wide settings for maximum core file size

Add the following line to `/etc/security/limits.conf`, or create `/etc/security/limits.d/gfarm.conf` and add the line to it.

```text
*       soft    core    unlimited
```

This setting will take effect after restarting the OS.

##### Setting the maximum size of core files for daemon processes

If you are internally calling Gfarm-related programs or using daemon processes that utilize the Gfarm library, you will also need to configure the settings for daemon processes. Change or add the following to the `DefaultLimitCORE` setting in `/etc/systemd/system.conf`.

```text
DefaultLimitCORE=infinity
```

This setting will take effect after restarting the OS.

If you want to apply the setting without restarting the OS, execute the following and then restart the daemon process with `systemctl restart`.

```console
# systemctl daemon-reload
```

### Testing

These instructions verify client operations for an existing Gfarm file system.  If you do not have one, please refer to the setup manual.

Place the Gfarm configuration file in `/etc/gfarm2.conf`.

#### Obtaining a JWT access token

Access the JWT Server to generate and store a JSON Web Token. The startup method and passphrase for `jwt-agent` will be displayed, so start `jwt-agent` as shown and enter the passphrase.

You can use the `jwt-parse` command to verify that the JWT access token has been obtained correctly.

```console
$ jwt-parse
```

If "token not found" or "expired" is displayed here, a valid token has not been obtained. Please try again.

#### Checking with the gfstatus command

You can check authentication to the metadata server with the `gfstatus` command.

```console
$ gfstatus
```

#### Checking with the gfhost command

You can check the connection to storage servers with the `gfhost` command. First, check UDP connectivity.

```console
$ gfhost -lvuU
```

The number on the far left indicates the server's load average. If it displays `-.--/-.--/-.--`, it indicates that the storage server is not running, and if it displays `x.xx/x.xx/x.xx`, it indicates that you cannot connect to that node.

Next, verify TCP connectivity and authentication.

```console
$ gfhost -lvu
```

The characters in the second column indicate the authentication result, with `x` indicating authentication failure and `-` indicating communication failure. If authentication using a token is successful, the result will be `A` or `a`. For details, refer to the [`gfhost` man page](https://oss-tsukuba.org/share/doc/gfarm/html/en/ref/man1/gfhost.1.html).

#### Checking with the gfarm2fs command

Check the mounting of the Gfarm file system with the gfarm2fs command.

```console
$ mkdir /tmp/gfarm
$ gfarm2fs /tmp/gfarm
$ cd /tmp/gfarm
$ echo a > a
$ ls
a
$ cat a
a
$ rm a
$ cd
$ fusermount -u /tmp/gfarm
$ rmdir /tmp/gfarm
```
