# Gfarm暗号化ファイルシステム

EncFS(*)を用いることにより、Gfarmファイルシステムに暗号化されたデータを
格納することができます。

(*) https://github.com/vgough/encfs/blob/master/encfs/encfs.pod

## インストール

EncFSをインストールします

    # yum install encfs

## 使い方

1. Gfarmファイルシステムをマウントする

       $ gfarm2fs /tmp/gfarm

この例では、/tmp/gfarmにGfarmファイルシステムをマウントしています。

2. 暗号化ファイルシステムを作成しマウントする

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

この例では、暗号化ファイルシステムをGfarmファイルシステムの .crypt ディ
レクトリに作成し、暗号化ファイルシステムを /tmp/crypt にマウントしてい
ます。暗号化されたデータは .crypt ディレクトリに格納されます。ディレク
トリ名は自由に決めてください。

3. アンマウントする

       $ fusermount -u /tmp/crypt

4. 再びマウントする

       $ encfs /tmp/gfarm/.crypt /tmp/crypt
       EncFS Password: *enter password*

再びマウントするときには、暗号化ファイルシステムのディレクトリとマウン
トポイントを指定します。この例では、Gfarmファイルシステムは /tmp/gfarm
にマウントされていることを仮定しています。

## もっと知りたい

EncFS についての詳細は以下を参照してください。

https://github.com/vgough/encfs/blob/master/encfs/encfs.pod
