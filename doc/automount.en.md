# Automounting the Gfarm file system

## About automount

Until now, it has been common for users to explicitly mount the Gfarm file system using gfarm2fs. Since Gfarm2fs-1.2.9.2, automounting has become available, so in this article, we will explain how it works. Automounting is a mechanism that automatically mounts the file system when you access the mount point using commands such as `cd` or `ls`.

For example, if /usr/users/tatebe is configured to be automatically mounted, it does not exist until it is accessed, but once it is accessed, it is automatically mounted.

```console
% ls -a /usr/users
.  ..
% ls -a /usr/users/tatebe
.  ..  .bash_logout  .bash_profile  .bashrc  .emacs
% ls -a /usr/users
.  ..  tatebe
```

This directory will be automatically unmounted if it remains inactive for a certain period of time.

This allows users to access the Gfarm file system without mounting it using gfarm2fs. Additionally, with Gfarm 2.5.8.2 or later, it is possible to automatically mount the user's home directory.

## Configuration of automount

This section describes the configuration for (Red Hat-based) Linux. First, copy `mount.gfarm2fs` to `/sbin`. The following example assumes that Gfarm is installed in `/usr/local`; please adjust the path accordingly.

```console
% sudo cp /usr/local/bin/mount.gfarm2fs /sbin
```

If autofs is not installed, install it.

```console
% sudo yum install autofs
```

Next, add the mount point and map file for the Gfarm file system to /etc/auto.master.

```text:/etc/auto.master
/gfarm /etc/auto.gfarm
```

In this example, the mount point for automount is set to /gfarm, and the map file is set to /etc/auto.gfarm. The /gfarm directory does not need to exist at this point. Next, configure the map file /etc/auto.gfarm.

```text:/etc/auto.gfarm
tatebe -fstype=gfarm2fs,allow_root,username=tatebe :/home/tatebe/.gfarm2rc
```

In this example, when /gfarm/tatebe is accessed, it is mounted with tatebe user privileges. The allow_root option permits root access to the mounted directory. If this option is not specified, the mount will not be automatically unmounted.

To enable the `allow_root` option, specify `user_allow_other` in `/etc/fuse.conf`.

```text:/etc/fuse.conf
user_allow_other
```

You can also use wildcards to create entries for all users.

```text:/etc/auto.gfarm
* -fstype=gfarm2fs,allow_root,username=& :/home/&/.gfarm2rc
```

That completes the setup. All you need to do now is restart (or reload) autofs, and it will be ready to use.

```console
# service autofs restart
```

## Changing the mount directory

By default, each user's home directory on the Gfarm file system is mounted. The home directory on the Gfarm file system is listed in the third column of the output from `gfuser -l`. If you want to mount a location other than this directory, specify it using the `gfarmfs_root` option.

```text:/etc/auto.gfarm
tatebe -fstype=gfarm2fs,allow_root,username=tatebe,gfarmfs_root=/ :/home/tatebe/.gfarm2rc
```

In this example, the / (root) directory of the Gfarm file system will be mounted.

## Access from other users

The mounted directory can only be accessed by the user specified with the `username` option. To allow other users to access it, use the `allow_other` option.

However, please note that even if you allow access from other users, their access permissions will be those of the user specified by the `username` option. In particular, if you are mounting a writable directory, other users will be able to write to it with the permissions of the user specified by the `username` option. Therefore, allowing access from other users is primarily appropriate when mounting read-only directories (or mounting them as read-only using the `ro` option).

Options are specified in the /etc/auto.gfarm file.

```text:/etc/auto.gfarm
tatebe -fstype=gfarm2fs,username=tatebe,allow_other,ro :/home/tatebe/.gfarm2rc
```

`allow_other` allows access from all users. `ro` is an option to mount the volume as read-only.

That completes the setup. All you need to do now is reload autofs, and it will be available.

```console
# service autofs reload
```

## Automounting the home directory

If the client node is not the same as the gfmd or gfsd node, the user's home directory can be automatically mounted.

In this case, since the configuration file ~/.gfarm2rc (and, if using shared key authentication, the key file ~/.gfarm_shared_key) cannot be accessed during login, you must create them in a location other than your home directory. For example, create them in /var/gfarm/home/tatebe as shown below.

```console
# mkdir -p /var/gfarm/home/tatebe
# cd /var/gfarm/home/tatebe
# echo shared_key_file /var/gfarm/home/tatebe/.gfarm_shared_key > .gfarm2rc
copy tatebe's shared secret key file to /var/gfarm/home/tatebe/.gfarm_shared_key
# chmod 600 .gfarm_shared_key
# chown -R tatebe .
```

First, create the directory /var/gfarm/home/tatebe and create the configuration file /var/gfarm/home/tatebe/.gfarm2rc. If you are using shared-key authentication, specify the location of the key file using the `shared_key_file` setting and copy the key file to that location. Finally, change the owner to the user.

After configuring these settings, specify the configuration files listed above in the /etc/auto.gfarm file.

```text:/etc/auto.gfarm
tatebe -fstype=gfarm2fs,username=tatebe,allow_root :/var/gfarm/home/tatebe/.gfarm2rc
```

The `allow_root` option is required for automatic unmounting and when logging in to this host using SSH public key authentication.

That completes the setup. All you need to do now is reload autofs, and it will be available.

```console
# service autofs reload
```

Note that while the automatically mounted home directory is accessible without any configuration, you must take care when configuring shared keys if you intend to use Gfarm commands such as `gfls` on a logged-in client. This is because, by default, Gfarm commands refer to the `.gfarm_shared_key` file in the home directory, which is located on the Gfarm file system.

Broadly speaking, there are two main approaches to consider.

1. Copy the private key to the home directory (of the Gfarm file system)

   ```console
   % cp -p /var/gfarm/home/tatebe/.gfarm_shared_key ~
   ```

1. Set the GFARM_CONFIG_FILE environment variable

   ```console
   % export GFARM_CONFIG_FILE=/var/gfarm/home/tatebe/.gfarm2rc
   ```

There aren’t many precautions to take when copying the shared key to the Gfarm file system, but please note that the environment variables must be set only when you log in to this host.

Now, go ahead and automount the Gfarm file system and enjoy the convenience it offers.
