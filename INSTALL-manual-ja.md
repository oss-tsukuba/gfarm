# Gfarm導入マニュアル

## はじめに

本書はGfarmの導入手順を示した文書です。Gfarmはメタデータサーバ、ファイルシステムノード、クライアントで構成され、それぞれのホストに導入します。

本書のコマンド例において、コマンドプロンプトが`#`の場合はrootでの実行、`$`の場合は一般ユーザでの実行を意味しています。

## 動作要件

Unix/Linux系の各種OSが利用可能ですが、本書ではRocky Linux 9、Ubuntu 24.04を前提に説明します。他のバージョンやOSの場合は適宜読替えてください。

## 導入・設定

### ポートの開放

デフォルトでは、メタデータサーバ（gfmd）にTCPポート601番、ファイルシステムノード（gfsd）にTCPポート600番、UDPポート600番で接続します。メタデータサーバ、ファイルシステムノードにおいてはそれらのポートについて外から接続可能にする必要があります。

外向きのポートに制限がある場合は上記のポートで接続できるよう開放します。

### Gfarm導入

#### 必要なパッケージのインストール

OSやパッケージを最新の状態にし、Gfarmクライアントの導入に必要なパッケージをインストールします。

以下はRocky Linux 9の場合です。

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

以下はUbuntu 24.04の場合です。

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

#### nscdのインストールと有効化

ネームサービスキャッシュデーモンnscdをインストールしてOSの起動とともに実行されるように設定します。nscdが起動していなくてもGfarmは利用可能ですが動作が遅くなることがあります。

以下はRocky Linux 9の場合です。

```console
# dnf -y install nscd
```

以下はUbuntu 24.04の場合です。

```console
# apt-get -y install nscd
```

nscdをOS起動時に実行されるようにして、起動します。

```console
# systemctl enable nscd
# systemctl start nscd
```

#### 時刻の同期

時刻がずれているとGfarmのアクセスができないため、NTP (Network Time Protocol)などにより時刻を合わせます。

#### CA証明書の設置

`/etc/pki/tls/certs/gfarm`にCA証明書`$CAHASH.0`を設置します。

#### cyrus-sasl-xoauth2-idpのインストール

以下からcyrus-sasl-xoauth2-idpのソースコードをダウンロードします。  
<https://github.com/oss-tsukuba/cyrus-sasl-xoauth2-idp/releases>

本書ではバージョン1.0.2で記載していますが、より新しいバージョンがリリースされていた場合、最新のバージョンを利用します。

任意のユーザ（rootである必要はない）でコンパイルした後、root権限でインストールします。

```console
$ wget --content-disposition https://github.com/oss-tsukuba/cyrus-sasl-xoauth2-idp/archive/1.0.2.tar.gz
$ tar pxf cyrus-sasl-xoauth2-idp-1.0.2.tar.gz
$ cd cyrus-sasl-xoauth2-idp-1.0.2
$ ./autogen.sh
$ ./configure --libdir=$(pkg-config --variable=libdir libsasl2)
$ make
$ sudo make install
```

メタデータサーバ、ファイルシステムノードの場合は設定ファイル`gfarm.conf`を設置します。

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

クライアントとして利用するホストでは設定ファイル`gfarm-client.conf`を設置します。

```console
# vi $(pkg-config --variable=libdir libsasl2)/sasl2/gfarm-client.conf
```

```text
xoauth2_user_claim: xxxx
```

`xxxx`には適切な値を設定してください。

#### Gfarmのインストール

以下からgfarmのソースコードをダウンロードします。  
<https://github.com/oss-tsukuba/gfarm/releases>

本書ではバージョン2.8.7で記載していますが、より新しいバージョンがリリースされていた場合、最新のバージョンを利用します。

任意のユーザ（rootである必要はない）でコンパイルした後、root権限でインストールします。

```console
$ wget --content-disposition https://github.com/oss-tsukuba/gfarm/archive/2.8.7.tar.gz
$ tar pxf gfarm-2.8.7.tar.gz
$ cd gfarm-2.8.7
$ ./configure --sysconfdir=/etc --enable-xmlattr --enable-tls13 --enable-cyrus-sasl
$ make
$ sudo make install
```

#### gfarm2fsのインストール

gfarm2fsはクライアントで必要となります。以下からgfarm2fsのソースコードをダウンロードします。  
<https://github.com/oss-tsukuba/gfarm2fs/releases>

本書ではバージョン1.2.22で記載していますが、より新しいバージョンがリリースされていた場合、最新のバージョンを利用します。

任意のユーザ（rootである必要はない）でコンパイルした後、root権限でインストールします。

```console
$ wget --content-disposition https://github.com/oss-tsukuba/gfarm2fs/archive/1.2.22.tar.gz
$ tar pxf gfarm2fs-1.2.22.tar.gz
$ cd gfarm2fs-1.2.22
$ ./configure --with-gfarm=/usr/local
$ make
$ sudo make install
```

#### jwt-agentのインストール

jwt-agentはクライアントで必要となります。以下からjwt-agentのソースコードをダウンロードします。  
<https://github.com/oss-tsukuba/jwt-agent/releases>

本書ではバージョン1.1.0で記載していますが、より新しいバージョンがリリースされていた場合、最新のバージョンを利用します。

任意のユーザ（rootである必要はない）でコンパイルした後、root権限でインストールします。

```console
$ wget --content-disposition https://github.com/oss-tsukuba/jwt-agent/archive/1.1.0.tar.gz
$ tar pxf jwt-agent-1.1.0.tar.gz
$ cd jwt-agent-1.1.0
$ make
$ sudo make PREFIX=/usr/local install
```

#### コアダンプファイルを出力する設定

問題が発生した場合の調査のためGfarm関連のファイルのコアダンプファイルおよびその時の実行ファイルを問題解決に十分な期間にわたってするまで保存するように設定します。

以下は、systemd-coredumpやabrtdのようなサービスを停止してコアダンプファイルを`/var/tmp`に保持する設定例です。

##### コアダンプ取得サービスの停止

以下はRocky Linux 9の場合です。

```console
# systemctl mask systemd-coredump.socket
```

以下はUbuntu 24.04の場合です。

```console
# systemctl stop apport
# systemctl disable apport
```

##### カーネルパラメータの確認

設定ファイル`/etc/sysctl.d/gfarm.conf`を以下の内容で作成します。

```text
fs.suid_dumpable = 1
kernel.core_pattern = /var/tmp/core.%e.%p
```

`kernel.core_pattern`行はコアファイルのパスを指定しています。ここで`%e`と`%p`はそれぞれプログラム名およびプロセスIDに展開されます。本ファイルを設置したあと、以下のように実行して設定を読み込みます。

```console
# /sbin/sysctl -p /etc/sysctl.d/gfarm.conf
```

##### コアファイルの最大サイズのシステム全体の設定

`/etc/security/limits.conf`に以下の行を追加、もしくは`/etc/security/limits.d/gfarm.conf`を作成して記載します。

```text
*       soft    core    unlimited
```

この設定はOS再起動後に有効になります。

##### コアファイルの最大サイズのデーモン・プロセス用の設定

Gfarm関係のプログラムを内部的に呼出すか、あるいはGfarmライブラリを利用するデーモンプロセスを利用している場合は、デーモン・プロセス用の設定も必要です。`/etc/systemd/system.conf`の`DefaultLimitCORE`の設定を以下のように変更ないし追加します。

```text
DefaultLimitCORE=infinity
```

この設定はOS再起動後に有効になります。

OSを再起動せず設定を適用したい場合は、以下を実行した後に`systemctl restart`でデーモン・プロセスを再起動します。

```console
# systemctl daemon-reload
```

### 動作確認

Gfarmがすでに構築されている環境に対し、クライアントとして動作確認を行う手順です。構築がまだの場合は構築マニュアルをご覧ください。

まず、Gfarmの設定ファイルを`/etc/gfarm2.conf`に設置します。

#### JWTアクセストークンの取得

JWT ServerにアクセスしてJSON Web Tokenを生成、格納します。`jwt-agent`の起動方法とパスフレーズが表示されますので、表示の通り`jwt-agent`を起動してパスフレーズを入力します。

正しくJWTアクセストークンが取得できているかどうかは`jwt-parse`コマンドで確認できます。

```console
$ jwt-parse
```

ここで、token not foundやexpiredが出力されたら有効なトークンが取得できていません。取得をやり直してください。

#### gfstatusコマンドによる確認

`gfstatus`コマンドによりメタデータサーバへの認証を確認することができます。

```console
$ gfstatus
```

#### gfhostコマンドによる確認

`gfhost`コマンドによりファイルシステムノードへの接続が確認できます。まずUDPの疎通を確認します。

```console
$ gfhost -lvuU
```

左端の数字はサーバのロードアベレージを示しています。ここが`-.--/-.--/-.--`と表示されている場合はファイルシステムノードのサーバが起動していないことを示し、`x.xx/x.xx/x.xx`と表示される場合はそのノードに接続できないことを示しています。

次にTCPの疎通と認証を確認します。

```console
$ gfhost -lvu
```

第2欄の文字は認証結果を示しており、`x`は認証失敗、`-`は疎通失敗を示しています。トークンによる認証が成功している場合は`A`または`a`となります。詳細は[`gfhost`のmanページ](https://oss-tsukuba.org/share/doc/gfarm/html/ja/ref/man1/gfhost.1.html)を参照してください。

#### gfarm2fsコマンドによる確認

gfarm2fsコマンドによるGfarmファイルシステムのマウントについて確認します。

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
