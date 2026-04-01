# Gfarm Setup Manual

## Introduction

This manual assumes that Gfarm has already been installed and is intended for setting up Gfarm.  First, set up the metadata servers that manage the filesystem metadata.  Then, set up the filesystem nodes and clients.

## Setting up Metadata Servers

The metadata server can be configured in a master-slave redundant setup across multiple servers.  Commands requiring execution as a privileged user on the local server are executed using sudo.

### Setting up a master metadata server

First, create the metadata server user and the filesystem node user on the local system at the master metadata server (assumed to be mds1). These usernames cannot be changed.

```console
mds1% sudo useradd -m _gfarmmd
mds1% sudo useradd -m _gfarmfs
```

For TLS communication, place the metadata server's certificate and private key in `/etc/pki/tls/certs/gfmd.crt` and `/etc/pki/tls/private/gfmd.key`.  The private key must be owned by root with permissions set to 0600.  Place the CA certificate in the `/etc/pki/tls/certs/gfarm` directory with the filename `$HASH.0`. The `$HASH` can be calculated using `openssl x509 -hash -noout -in $CRT`, where `$CRT` is the CA certificate.

If certificates are not installed, all communications will be in plaintext.  Authentication uses Gfarm's proprietary shared key method, requiring each user to generate a shared key.  Below, we generate a shared key valid for one year (=31,536,000 seconds).

```console
mds1% sudo -u _gfarmmd gfkey -f -p 31536000
mds1% sudo -u _gfarmfs gfkey -f -p 31536000
```

The shared key is created in `$HOME/.gfarm_shared_key`. The `_gfarmmd` shared key must be copied to all metadata servers, and the `_gfarmfs` shared key must be copied to all metadata servers and filesystem nodes with the same owner and permissions. Additionally, it must be updated before its expiration date.

Create the Gfarm file system.

```console
mds1% sudo config-gfarm -A $USER -r -X -d md5
```

`config-gfarm` generates the necessary Gfarm configuration files (gfarm2.conf and gfmd.conf), configures the PostgreSQL backend database, and installs the startup scripts for the gfmd metadata server and the PostgreSQL server.  The configuration files are placed in the directory specified by the `--sysconfdir` option during Gfarm installation.  If not specified, they are placed in the directory specified by the `--prefix` option.  If we denote the `--prefix` directory as `$PREFIX`, the configuration files are placed in `$PREFIX/etc`.  The default for `$PREFIX` is /usr/local.

<!-- >
The -N option prevents server startup when executing config-gfarm.
< -->
The -A option specifies the initial user for the Gfarm file system.  Use `$USER` to specify a general user account on the master metadata server.  The -r option configures settings for metadata server redundancy.  The -X option enables extended XML attributes.  The -d option enables settings to ensure data integrity and specifies the digest algorithm.  For details on `config-gfarm`, refer to the manual page.

<!-- >
Next, add the authentication method to enable for communication with the metadata server to gfmd.conf.

```text
auth enable tls_client_certificate *
auth enable tls_sharedsecret *
auth enable sasl *
auth enable sasl_auth *
auth enable kerberos *
auth enable kerberos_auth *
sasl_mechanisms XOAUTH2
```
< -->
<!-- >
If you are using TLS communication and have chosen not to use the service certificate (one with a distinguished name (DN) starting with "gfsd/", such as "DN=gfsd/host.domain"), and are instead using the server certificate, delete the following two lines.

```text
spool_server_cred_type host
spool_server_cred_service gfsd
```
< -->
<!-- >
Start the backend database and metadata server, and configure them to start automatically at system startup.

```console
mds1% sudo systemctl start gfarm-pgsql
mds1% sudo systemctl enable gfarm-pgsql
mds1% sudo systemctl start gfmd
mds1% sudo systemctl enable gfmd
```
< -->
Configure the backend database and metadata server to start automatically at system startup.

```console
mds1% sudo systemctl enable gfarm-pgsql
mds1% sudo systemctl enable gfmd
```

Next, update gfarm2.conf to add the list of metadata servers for the redundant configuration and the authentication method to enable.  The following is an example when the FQDNs of the metadata servers in the redundant configuration are set to mds1.example.com, mds2.example.com, and mds3.example.com.

```text
metadb_server_list mds1.example.com:601 mds2.example.com:601 mds3.example.com:601
sasl_mechanisms XOAUTH2
```

In specifying the metadata server, `:601` indicates the TCP port number to use; it can be omitted when using the default port 601.
<!-- >
If you are using TLS communication and have chosen not to use the service certificate (one with a distinguished name (DN) starting with "gfsd/", such as "DN=gfsd/host.domain"), and are instead using the server certificate, delete the following two lines.

```text
spool_server_cred_type host
spool_server_cred_service gfsd
```
< -->
When configuring Gfarm with the -A option, the local user specified for the master metadata server is created as the initial user.  This user can access Gfarm with administrative privileges (gfarmadm permissions).  Note that no other users initially are created, so only this user can access the master metadata server.  Perform subsequent configuration while logged in as this user.

Add master and slave metadata servers.  In the following example, mds1.example.com is the master server, while mds2.example.com and mds3.example.com are synchronous and asynchronous slave servers, respectively.  Metadata servers can be managed using freely assignable site names.  Slaves in the same site as the master are synchronous, while slaves in different sites are asynchronous.  In the example below, the master's site name is "east", and the asynchronous slave's site name is "west".

```console
mds1% gfmdhost -m mds1.example.com -C east
mds1% gfmdhost -c mds2.example.com -C east
mds1% gfmdhost -c mds3.example.com -C west
```

### Setting up slave metadata servers

We will now proceed with configuring the slave metadata servers.  First, we will take a dump of the metadata from the master metadata server and transfer it to each slave server.

```console
mds1% sudo gfdump.postgresql -d -f dumpfile
mds1% scp dumpfile mds2.example.com:
mds1% scp dumpfile mds3.example.com:
mds1% sudo rm dumpfile
```

On each slave server (mds2, mds3), configure the slave metadata server as follows.  First, create the metadata server user and the filesystem node user locally on the slave metadata server.  These usernames cannot be changed.

```console
mds[23]% sudo useradd -m _gfarmmd
mds[23]% sudo useradd -m _gfarmfs
```

For TLS communication, place the metadata server's certificate and private key in `/etc/pki/tls/certs/gfmd.crt` and `/etc/pki/tls/private/gfmd.key`.  The private key must be owned by root with permissions set to 0600.  Place the CA certificate in the `/etc/pki/tls/certs/gfarm` directory with the filename `$HASH.0`.

When using the shared key method, copy the shared key `$HOME/.gfarm_shared_key` for `_gfarmmd` and `_gfarmfs` from the metadata server.  Verify that the owner is set to `_gfarmmd` and `_gfarmfs` respectively, and that the permissions are set to 0600.

Perform the Gfarm setup in the same manner as for the master metadata server.

```console
mds[23]% sudo config-gfarm -N -A $USER -r -X -d md5
```

Specify the same options for config-gfarm as when configuring the metadata server.
The -N option prevents server startup when executing config-gfarm.
<!-- >
Next, add the authentication methods to enable, similar to the master, to gfmd.conf.

```text
auth enable tls_client_certificate *
auth enable tls_sharedsecret *
auth enable sasl *
auth enable sasl_auth *
auth enable kerberos *
auth enable kerberos_auth *
sasl_mechanisms XOAUTH2
```
< -->
<!-- >
If you are using TLS communication and have chosen not to use the service certificate (one with a distinguished name (DN) starting with "gfsd/", such as "DN=gfsd/host.domain"), and are instead using the server certificate, delete the following two lines.

```text
spool_server_cred_type host
spool_server_cred_service gfsd
```
< -->
Unlike when configuring the master, only the backend database is started.

```console
mds[23]% sudo systemctl start gfarm-pgsql
mds[23]% sudo systemctl enable gfarm-pgsql
```

Update (restore) the backend database using the metadata dump file.  After restoration, the dump file is no longer needed and should be deleted.

```console
mds[23]% sudo gfdump.postgresql -r -f dumpfile
mds[23]% rm dumpfile
```

Running `gfdump.postgresql -r` to restore metadata will start the metadata server.  Configure it to start automatically at system boot.

```console
mds[23]% sudo systemctl enable gfmd
```

Copy `gfarm2.conf` from the metadata server and overwrite it.

### Testing

After completing the above setup on all slave servers, you can verify that the slave metadata servers are functioning correctly by executing the following on the initial user of the master metadata server.

```console
mds1% gfmdhost -l
+ master -     m east         mds1.example.com 601
+ slave  sync  c east         mds2.example.com 601
+ slave  async c west         mds3.example.com 601
```

If the first column of the slave metadata server shows a `+`, then it is synchronized correctly.  If it shows an `x` or `e`, synchronization has failed. Please restore using the latest dump file.  Note that you can add slave metadata servers at any time using the latest dump file.

## Setting up filesystem nodes

By making the server a Gfarm filesystem node, you can incorporate its local storage into the Gfarm file system.

First, create a user for the filesystem node on the local system. This username cannot be changed.

```console
fs% sudo useradd -m _gfarmfs
```

For TLS communication, place the service certificate and private key for the filesystem node in `/etc/grid-security/gfsd/gfsdcert.pem` and `/etc/grid-security/gfsd/gfsdkey.pem`.  Set the owner under the `/etc/grid-security/gfsd` directory to `_gfarmfs` and the permissions for the private key to 0600.  The service certificate must have a DN starting with `gfsd/`, such as `DN=gfsd/host.domain`.  Place the CA certificate in the `/etc/pki/tls/certs/gfarm` directory with the filename `$HASH.0`.

When using the shared key method, copy the shared key `$HOME/.gfarm_shared_key` for `_gfarmfs` from the metadata server.  Verify that the owner is `_gfarmfs` and the permissions are 0600.

Copy the configuration file `gfarm2.conf` from the metadata server, then configure the filesystem node using `config-gfsd`. The following example shows incorporating the directory `/var/spool/gfarm` and its subdirectories into the Gfarm filesystem on the filesystem node fs1.exemple.com.

```console
fs% sudo config-gfsd /var/spool/gfarm
created /var/spool/gfarm
created /etc/systemd/system/gfsd.service
created /usr/local/etc/unconfig-gfsd.sh
config-gfsd success

Please ask admin_user to register your host by the following command:

/usr/local/bin/gfhost -c -a x86_64-debiantrixie_sid-linux -p 600 -n 22 fs1.example.com

After that, start gfsd by the following command as a root:

systemctl start gfsd.service
```

The specified directory must be either not exist or be empty.  Only the `_gfarmfs` user and privileged users on the local system will be able to access this directory and its contents.  For details on `config-gfsd`, refer to the manual page.

Execute the displayed command (`gfhost -c -a ...`) as the initial user on the master metadata server to register the filesystem node.
<!-- >
Return to the filesystem node and delete the following line from `/etc/systemd/system/gfsd.service`.

```text
Environment=X509_USER_CERT=/etc/grid-security/gfsd/gfsdcert.pem
Environment=X509_USER_KEY=/etc/grid-security/gfsd/gfsdkey.pem
```
< -->
Return to the filesystem node and start the gfsd filesystem server.

```console
fs% sudo systemctl start gfsd.service
fs% sudo systemctl enable gfsd.service
```

### Testing

After performing the above setup on all filesystem nodes, you can verify that the filesystem server is functioning correctly by executing the following on the initial user of the master metadata server.

```console
mds1% gfdf
    1K-blocks          Used         Avail Use% Host
   1002059396      83752636     918306760   8% fs1.example.com
   1002059396      83752636     918306760   8% fs2.example.com
   1002059396      83752636     918306760   8% fs3.example.com
   1002059396      83752636     918306760   8% fs4.example.com
----------------------------------------------
   4008237584     335010544    3673227040   8%
```

If all filesystem nodes are displayed, the setup is correct.  Note that filesystem nodes can be added at any time.

## User registration

After applying the above settings, the initial user on the master metadata server can now access the metadata server using the shared key.  Consequently, you can run `gfmdhost` and `gfdf`, but authentication to the filesystem nodes is still not possible.

To register a user, you must first register the user with Gfarm and configure authentication settings.  The initial user is already registered, but other users must be registered using `gfuser`. Registration must be performed by a Gfarm administrative privilege user (currently the initial user).  The following example creates a user named Taro Yamada with the username `taro`, sets the home directory to `/home/taro`, and assigns the client certificate `/O=Gfarm/OU=Test/OU=CA/CN=taro`.

```console
mds1% gfuser -c taro "Taro Yamada" /home/taro /O=Gfarm/OU=Test/OU=CA/CN=taro
mds1% gfmkdir -p /home/taro
mds1% gfchown taro /home/taro
```

If you are using a client certificate for authentication, you are done here.  You can leave this "" for now, as the client certificate can be changed later.

If using a token, register the ID obtained from the token.  If the ID is the same as the username (e.g., `taro`), this is unnecessary.  Otherwise, add that ID using `gfuser -A`.

```console
mds1% gfuser -A taro SASL $ID
```

Which token attribute serves as the ID depends on the configuration of cyrus-sasl-xoauth2-idp.  For token-based authentication, this completes the setup.

When using a shared key, you must create a home directory on all hosts and place the shared key there.  Ensure the shared key is owned by a local user with permissions set to 0600.  The local username can differ from the Gfarm username, but in that case, you must create a map file.

### Administrative privilege user

To perform configuration changes such as creating users or registering filesystem nodes, you must execute them as an administrative privilege user.  Administrative privilege users are those belonging to the `gfarmadm` group.  Group management is performed using the `gfgroup` command.  For details, refer to the `gfgroup` manual page.

### Access privilege user

There are also access privilege users for managing file data, not for changing settings.  Access privilege users are those belonging to the `gfarmroot` group.

`gfsudo` allows uers to temporarily become an access privilege user and execute commands.  `gfsudo` is available to administrative privilege users.

## Setting up clients

The required configuration for the client is to place `gfarm2.conf` in either `$PREFIX/etc/gfarm2.conf` or `$HOME/.gfarm2rc`. Additionally, a client certificate for authentication, an access token, and a shared key are required.

## Testing

First, configure the authentication settings for each registered user.  For client certificates, place a proxy certificate in `/tmp/x509up_u$UID` or the location specified by the environment variable `X509_USER_PROXY`. For tokens, either obtain the token using `jwt-agent`, or place the token in `/tmp/jwt_user_u$UID/token.jwt` or the location specified by the environment variable `JWT_USER_PASS`. For shared keys, place them in `$HOME/.gfarm_shared_key`.

After completing the authentication configuration, you can verify the operation as follows.

### Authentication testing for the metadata server

You can check authentication to the metadata server with the `gfstatus` command.

```console
% gfstatus
```

### Authentication testing for filesytem nodes

You can check the connection to filesyste servers with the `gfhost` command. First, check UDP connectivity.

```console
% gfhost -lvuU
```

The number on the far left indicates the server's load average. If it displays `-.--/-.--/-.--`, it indicates that the filesystem server is not running, and if it displays `x.xx/x.xx/x.xx`, it indicates that you cannot connect to that node.

Next, verify TCP connectivity and authentication.

```console
% gfhost -lvu
```

The characters in the second column indicate the authentication result, with `x` indicating authentication failure and `-` indicating communication failure. If authentication using a token is successful, the result will be `A` or `a`. For details, refer to the [`gfhost` man page](https://oss-tsukuba.org/share/doc/gfarm/html/en/ref/man1/gfhost.1.html).

## Basic usage

In addition to mounting and using it, you can configure file replication, perform high-speed copying of large numbers of files, and create high-speed archives.

### Mounting

The Gfarm file system can be mounted using `gfarm2fs`.

```console
% mkdir /tmp/foo
% gfarm2fs /tmp/foo
```

After mounting, you can perform file operations using standard commands. Unmounting is done using the `fusermount` command.

```console
% fusermount -u /tmp/foo
```

For details, refer to the documentation included with `gfarm2fs`.

### File replication

You can set the number of file replicas using the `gfncopy` command. The following configuration maintains two replicas for all files under the `/` directory.

```console
% gfncopy -s 2 /
```

You can determine the file system nodes where files are stored using the `gfwhere` command.

```console
% gfwhere test
linux-1.example.com linux-2.example.com
```

In this example, files are stored on `linux-1` and `linux-2`.

### High-Speed Copy Command - gfpcopy

This command is used to copy large numbers of (small) files. For details, refer to the `gfpcopy` manual page.

### High-Speed Archive Command - gfptar

This command is used to quickly store large numbers of (small) files on Gfarm. For details, refer to the `gfptar` manual page.

## Upgrade Procedure

When updating Gfarm, perform the upgrade in the following order: metadata servers, file system nodes, and clients.

### Updating metadata servers

If a slave metadata server is running, you can upgrade without stopping operations. First, stop the slave metadata server and perform the upgrade via an overwriting installation. The backend PostgreSQL instance can remain running. After the overwriting installation, execute `config-gfarm-update --update` to start the metadata server.

```console
mds2% sudo systemctl stop gfmd
# Overwrite installation of Gfarm
mds2% sudo config-gfarm-update --update
mds2% sudo systemctl start gfmd
```

Verify that the slave metadata server has synchronized using `gfmdhost`, then fail over the metadata server from the master to the slave.

```console
mds1% gfmdhost -l
+ master -     m east         mds1.example.com 601
+ slave  sync  c east         mds2.example.com 601
+ slave  async c west         mds3.example.com 601
mds1% sudo systemctl stop gfmd
mds2% sudo pkill -USR1 gfmd
mds2% gfmdhost -l
- slave  sync  c east         mds1.example.com 601
+ master -     m east         mds2.example.com 601
+ slave  async c west         mds3.example.com 601
```

Update the old master metadata server.

```console
# Overwrite installation of Gfarm
mds1% sudo config-gfarm-update --update
mds1% sudo systemctl start gfmd
mds1% gfmdhost -l
+ slave  sync  c east         mds1.example.com 601
+ master -     m east         mds2.example.com 601
+ slave  async c west         mds3.example.com 601
```

The master metadata server will remain on mds2, and continuing operations as is will not cause any issues. Alternatively, reverting the master to mds1 is also an option. Updating asynchronous slave metadata servers requires stopping the server and performing an overwriting installation.

```console
mds3% sudo systemctl stop gfmd
# Overwrite installation of Gfarm
mds3% sudo config-gfarm-update --update
mds3% sudo systemctl start gfmd
```

Verify that the slave metadata server has synchronized using `gfmdhost`.

### Updating file system nodes

For file system nodes, if file replicas have been created, it is possible to update them one by one while continuing operations. Shut down the file system node server and perform an overwrite installation to upgrade.

```console
fs% sudo systemctl stop gfsd
# Overwrite installation of Gfarm
fs% sudo systemctl start gfsd
```

### Updating clients

Unmount the Gfarm file system mounted by each user, and then install Gfarm and gfarm2fs by overwriting the existing files.
