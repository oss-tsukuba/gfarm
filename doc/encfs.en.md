# Gfarm Encrypted File System

Encrypted data can be stored in Gfarm file system using
EncFS(*).

(*) https://github.com/vgough/encfs/blob/master/encfs/encfs.pod

## Installation

EncFS needs to be installed

    # yum install encfs

## How to use

1. Mount a Gfarm File System

       $ gfarm2fs /tmp/gfarm

This example mounts the Gfarm file system at /tmp/gfarm.

2. Create and mount an encrypted file system

       $ encfs /tmp/gfarm/.crypt /tmp/crypt
       The directory "/tmp/gfarm/.crypt/" does not exist. Should it be created? (y,N) y
       The directory "/tmp/crypt/" does not exist. Should it be created? (y,N) y
       Creating new encrypted volume.
       Please choose from one of the following options:
        enter "x" for expert configuration mode,
        enter "p" for pre-configured paranoia mode,
        anything else, or an empty line will select standard mode.
       ?>
       
       Standard configuration selected.
       
       Configuration finished.  The filesystem to be created has
       the following properties:
       Filesystem cipher: "ssl/aes", version 3:0:2
       Filename encoding: "nameio/block", version 4:0:2
       Key Size: 192 bits
       Block Size: 1024 bytes
       Each file contains 8 byte header with unique IV data.
       Filenames encoded using IV chaining mode.
       File holes passed through to ciphertext.
       
       Now you will need to enter a password for your filesystem.
       You will need to remember this password, as there is absolutely
       no recovery mechanism.  However, the password can be changed
       later using encfsctl.
       
       New Encfs Password: *enter password*
       Verify Encfs Password: *enter password again*

This example creates an encrypted file system in .crypt directory in
the Gfarm file system, and mounts it at /tmp/crypt.  Encrypted files
are stored in .crypt directory.  You can use your favorite directories
for each.

3. Umount the encrypted file system

       $ fusermount -u /tmp/crypt

4. Mount again

       $ encfs /tmp/gfarm/.crypt /tmp/crypt
       EncFS Password: *enter password*

You need to specify a directory of the encrypted file system and the
mount point.  This assumes the Gfarm file system is mounted at
/tmp/gfarm.

## More details

See https://github.com/vgough/encfs/blob/master/encfs/encfs.pod
