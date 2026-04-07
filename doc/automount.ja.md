# Gfarmファイルシステムをautomountする

## automountについて

Gfarmファイルシステムは、これまでユーザがgfarm2fsで明示的にマウントして用いることが一般的でした。Gfarm2fs-1.2.9.2以降ではautomountもできるようになりましたので紹介します。automountは、cdやlsなどでマウントポイントをアクセスすると自動的にマウントする仕組みです。

たとえば、/usr/users/tatebeがautomountされる設定となっている場合、/usr/users/tatebeはアクセスされるまでは存在しませんが、一度アクセスするとアクセスすると自動的にマウントされます。

```console
% ls -a /usr/users
.  ..
% ls -a /usr/users/tatebe
.  ..  .bash_logout  .bash_profile  .bashrc  .emacs
% ls -a /usr/users
.  ..  tatebe
```

このディレクトリは一定時間アクセスしないと自動的にアンマウントされます。

これにより、ユーザがgfarm2fsでマウントすることなく、Gfarmファイルシステムをアクセスできるようになります。また、Gfarm-2.5.8.2以降を利用するとユーザのホームディレクトリをautomountすることも可能です。

## automountの設定

本節では、(RedHat系の)Linuxの設定を紹介します。まず、mount.gfarm2fsを/sbinにコピーします。以下の例ではGfarmは/usr/localにインストールされていることを仮定していますが、適宜読み替えて下さい。

```console
% sudo cp /usr/local/bin/mount.gfarm2fs /sbin
```

もしautofsが入っていなければ、autofsをいれます。

```console
% sudo yum install autofs
```

次に、/etc/auto.masterにGfarmファイルシステム用のマウントポイントとマップファイルを記載します。

```text:/etc/auto.master
/gfarm /etc/auto.gfarm
```

この例では、automountのマウントポイントは/gfarm、マップファイルは/etc/auto.gfarmを指定しています。このとき、ディレクトリ/gfarmは存在していなくても構いません。次に、マップファイル/etc/auto.gfarmを設定します。

```text:/etc/auto.gfarm
tatebe -fstype=gfarm2fs,allow_root,username=tatebe :/home/tatebe/.gfarm2rc
```

この例では、/gfarm/tatebeがアクセスされたとき、tatebeユーザ権限でマウントします。allow_rootオプションは、rootがマウントしたディレクトリへのアクセスを許すオプションです。このオプションを付けないと自動的にumountされません。

allow_rootオプションを利用可能にするため、/etc/fuse.confでuser_allow_otherを指定します。

```text:/etc/fuse.conf
user_allow_other
```

また、ワイルドカードを利用して全ユーザ用のエントリを作成することも可能です。

```text:/etc/auto.gfarm
* -fstype=gfarm2fs,allow_root,username=& :/home/&/.gfarm2rc
```

これで設定は完了です。あとはautofsをrestart (reload)すると利用可能になります。

```console
# service autofs restart
```

## マウントディレクトリの変更

デフォルトでは、それぞれのユーザのGfarmファイルシステム上のホームディレクトリがマウントされます。Gfarmファイルシステム上のホームディレクトリは、gfuser -lの出力の第三カラムです。このディレクトリ以外の場所をマウントしたい場合は、gfarmfs_rootオプションで指定します。

```text:/etc/auto.gfarm
tatebe -fstype=gfarm2fs,allow_root,username=tatebe,gfarmfs_root=/ :/home/tatebe/.gfarm2rc
```

この例ではGfarmファイルシステムの/（ルート）ディレクトリをマウントします。

## 他ユーザからのアクセス

マウントしたディレクトリは、usernameオプションで指定したユーザしかアクセスすることができません。他のユーザがアクセスするためには、allow_otherオプションをつけます。

ただし、他のユーザからのアクセスを可能としても、アクセスする権限はusernameオプションで指定したユーザ権限となることに注意して下さい。特に書き込み可能なディレクトリをマウントしている場合は、usernameオプションで指定したユーザの権限で他のユーザが書き込めてしまいます。従って、他ユーザからのアクセスを許すのは、読込オンリーのディレクトリをマウント（あるいはroオプションをつけて読込オンリーでマウント）するケースが主となります。

オプションは/etc/auto.gfarmファイルで指定します。

```text:/etc/auto.gfarm
tatebe -fstype=gfarm2fs,username=tatebe,allow_other,ro :/home/tatebe/.gfarm2rc
```

allow_otherは全ユーザからのアクセスを可能とします。roは読込オンリーでマウントするオプションです。

これで設定は完了です。あとはautofsをreloadすると利用可能になります。

```console
# service autofs reload
```

## ホームディレクトリのautomount

gfmdノード、gfsdノードと同一ノードではないクライアントノードの場合は、ユーザのホームディレクトリをautomountすることができます。

この場合、ログイン時にautomountするとき、設定ファイル~/.gfarm2rc（と共有鍵認証の場合は鍵ファイル~/.gfarm_shared_key）が参照できないため、それらをホームとは別の場所に作成する必要があります。たとえば/var/gfarm/home/tatebeに以下のように作成します。

```console
# mkdir -p /var/gfarm/home/tatebe
# cd /var/gfarm/home/tatebe
# echo shared_key_file /var/gfarm/home/tatebe/.gfarm_shared_key > .gfarm2rc
copy tatebe's shared secret key file to /var/gfarm/home/tatebe/.gfarm_shared_key
# chmod 600 .gfarm_shared_key
# chown -R tatebe .
```

まずディレクトリ/var/gfarm/home/tatebeを作成し、設定ファイル/var/gfarm/home/tatebe/.gfarm2rcを作成します。共有鍵認証を行う場合は、鍵ファイルの場所をshared_key_fileで指定し、指定した場所に鍵ファイルをコピーします。最後に所有者をユーザに変更します。

これらの設定をした後、/etc/auto.gfarmファイルで上記の設定ファイルを指定します。

```text:/etc/auto.gfarm
tatebe -fstype=gfarm2fs,username=tatebe,allow_root :/var/gfarm/home/tatebe/.gfarm2rc
```

allow_rootは、自動umountのため、またsshの公開鍵認証でこのホストにloginする場合などに必要となります。

これで設定は完了です。あとはautofsをreloadすると利用可能になります。

```console
# service autofs reload
```

なお、automountしたホームディレクトリはなにも設定しなくてもアクセス可能ですが、loginしたクライアントでgflsなどのGfarmコマンドを利用する場合は、共有鍵の設定で注意が必要です。Gfarmコマンドは、デフォルトではホームディレクトリの.gfarm_shared_keyを参照しますが、これがGfarmファイルシステムにあるためです。

大きく次の二つの対処方法が考えられます。

1. 秘密鍵を（Gfarmファイルシステムの）ホームディレクトリにコピーする

   ```console
   % cp -p /var/gfarm/home/tatebe/.gfarm_shared_key ~
   ```

1. GFARM_CONFIG_FILE環境変数を設定する

   ```console
   % export GFARM_CONFIG_FILE=/var/gfarm/home/tatebe/.gfarm2rc
   ```

共有鍵をGfarmファイルシステムにコピーする方法は、あまり注意点がありませんが、環境変数の方はこのホストにログインした時だけ設定するようにする必要がありますので、注意してください。

それではGfarmファイルシステムをautomountして快適ライフをお楽しみ下さい。
