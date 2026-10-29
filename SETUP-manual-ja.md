# Gfarm設定マニュアル

## はじめに

本マニュアルはGfarmが導入されていることを前提として、Gfarmの構築を行うためのものです。Gfarmを構築するために、まずファイルシステムメタデータを管理するメタデータサーバの構築を行います。その後、ファイルシステムノード、クライアントの構築を行います。

### サービス操作コマンドの読み替え

本マニュアルは systemd を用いる Linux を前提としています。
それ以外のシステム（FreeBSD、NetBSD、macOS、SysVinit を用いる Linux など）や、
config-gfarm で `--prefix` オプションを指定した場合は、
`sudo systemctl 操作 サービス名` を、
root で実行する `$RC_DIR/サービス名 操作` に読み替えてください。
ただし `systemctl start gfmd-slave` は `$RC_DIR/gfmd slavestart` となります。  
`systemctl enable` に相当する設定は `config-gfarm` が行います
（`--prefix` 指定時および自動 enable が未実装の macOS は除く）。

| OS             | $RC_DIR               |
| -------------- | --------------------- |
| FreeBSD, macOS | `/usr/local/etc/rc.d` |
| NetBSD         | `/etc/rc.d`           |
| その他         | `/etc/init.d`         |

`--prefix` を指定した場合は `<prefix>/etc/rc.d`（FreeBSD、NetBSD、macOS）
または `<prefix>/etc/init.d`（その他）となります。

## メタデータサーバ設定

メタデータサーバは複数のサーバでマスタースレーブ型の冗長構成をとることができます。以下、ローカルサーバの特権ユーザで実行する必要のあるコマンドはsudoで実行しています。

### マスターメタデータサーバの設定

まず、マスターメタデータサーバ（mds1とします）においてメタデータサーバ用ユーザとファイルシステムノード用ユーザをローカルシステムに作成します。このユーザ名は変更できません。

```console
mds1% sudo useradd -m _gfarmmd
mds1% sudo useradd -m _gfarmfs
```

TLS通信を利用する場合メタデータサーバの証明書と秘密鍵を`/etc/pki/tls/certs/gfmd.crt`と`/etc/pki/tls/private/gfmd.key`に設置します。秘密鍵はrootの所有でパーミッションは0600とします。CAの証明書を`/etc/pki/tls/certs/gfarm`ディレクトリにファイル名を`$HASH.0`として設置します。`$HASH`は`openssl x509 -hash -noout -in $CRT`で計算できます。`$CRT`はCAの証明書です。

証明書を設置しない場合、TLS通信は利用できず、全ての通信は平文となります。認証はGfarm独自の共有鍵方式となり、それぞれのユーザにおいて共有鍵を作成する必要があります。以下では1年間（=31,536,000秒）有効な共有鍵を作成しています。

```console
mds1% sudo -u _gfarmmd gfkey -f -p 31536000
mds1% sudo -u _gfarmfs gfkey -f -p 31536000
```

共有鍵は`$HOME/.gfarm_shared_key`に作成され、`_gfarmmd`の共有鍵は全てのメタデータサーバ、`_gfarmfs`の共有鍵は全てのメタデータサーバ、ファイルシステムノードに同じ所有者と同じパーミッションでコピーしておく必要があります。また、有効期限が過ぎる前に更新が必要です。

Gfarmファイルシステムを構築します。

```console
mds1% sudo config-gfarm -A $USER -r -X -d md5
```

`config-gfarm`によりGfarmに必要な設定ファイル（gfarm2.confとgfmd.conf）およびバックエンドデータベースであるPostgreSQLの設定がなされ、メタデータサーバgfmdとPostgreSQLサーバの起動スクリプトが設置されます。設定ファイルはGfarmの構築時に`--sysconfdir`オプションにより指定したディレクトリ、指定していない場合は`--prefix`で指定したディレクトリを`$PREFIX`とすると`$PREFIX/etc`となります。`$PREFIX`のデフォルトは/usr/localです。導入マニュアルに従ってGfarmを導入した場合は/etcとなります。

<!-- >
-Nオプションはconfig-gfarm実行時にサーバ起動を行わないオプションです。
< -->
-AオプションでGfarmファイルシステムにおける初期ユーザを指定します。`$USER`ではマスターメタデータサーバの一般ユーザアカウントを指定してください。-rオプションでメタデータサーバの冗長構成用の設定を行います。-Xオプションで拡張XML属性を有効にします。-dオプションでデータ完全性を保証するための設定を有効とし、そのダイジェストのアルゴリズムを指定します。`config-gfarm`の詳細はマニュアルページを参照してください。

<!-- >
次に、メタデータサーバとの通信において有効にする認証方式を`gfmd.conf`に追加します。

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
TLS通信を用いる場合で、gfsdのサービス証明書（DN=gfsd/host.domainのようにDNにgfsd/がついたもの）を用いないで、サーバ証明書を用いる場合は以下の2行を削除します。

```text
spool_server_cred_type host
spool_server_cred_service gfsd
```
< -->
<!-- >
バックエンドデータベースとメタデータサーバを起動し、またシステム起動時に自動的に起動するように設定します。

```console
mds1% sudo systemctl start gfarm-pgsql
mds1% sudo systemctl enable gfarm-pgsql
mds1% sudo systemctl start gfmd
mds1% sudo systemctl enable gfmd
```
< -->
次に、`gfarm2.conf`を更新して、冗長構成をとるメタデータサーバのリストと有効にする認証方式を追加します。以下は冗長構成のメタデータサーバのFQDNをmds1.example.com, mds2.example.com, mds3.example.comとしたときの例です。

```text
metadb_server_list mds1.example.com:601 mds2.example.com:601 mds3.example.com:601
sasl_mechanisms XOAUTH2
auth_trial_order tls_client_certificate tls_sharedsecret sasl_auth sasl sharedsecret
```

メタデータサーバの指定における`:601`は使用するTCPポート番号を表し、デフォルトの601番を用いる場合は省略可能です。
`auth_trial_order`は認証方式を試す順番を指定します。デフォルトではTLSを用いない共有鍵方式が最初に試されるため、TLS通信を利用するために指定します。スレーブメタデータサーバとマスターメタデータサーバの間の接続のため、全てのメタデータサーバの`gfmd.conf`にも同じ`auth_trial_order`の行を追加します。
<!-- >
TLS通信を用いる場合で、gfsdのサービス証明書（DN=gfsd/host.domainのようにDNにgfsd/がついたもの）を用いないで、サーバ証明書を用いる場合は以下の2行を削除します。

```text
spool_server_cred_type host
spool_server_cred_service gfsd
```
< -->
`config-gfarm`のときに-Aで指定したマスターメタデータサーバのローカルユーザは初期ユーザとして作成され、Gfarmを管理特権モード（gfarmadm権限）でアクセス可能です。なお、初期状態では他のユーザは作成されていないので、このユーザでのみマスターメタデータサーバでのアクセスが可能です。そのユーザになって以降の設定を実施します。

マスター、スレーブメタデータサーバを追加します。以下の例ではmds1.example.comがマスター、mds2.example.comが同期スレーブ、mds3.example.comが非同期スレーブとします。メタデータサーバは自由に指定可能なサイト名で管理することができ、マスターと同一サイトのスレーブは同期スレーブ、異なるサイトのスレーブは非同期スレーブとなります。以下ではマスターのサイト名をeast、非同期スレーブのサイト名をwestとしています。

```console
mds1% gfmdhost -m mds1.example.com -C east
mds1% gfmdhost -c mds2.example.com -C east
mds1% gfmdhost -c mds3.example.com -C west
```

### スレーブメタデータサーバの設定

これからスレーブメタデータサーバの設定を実施します。そのためにまずマスターメタデータサーバのメタデータのダンプを取り、それぞれのスレーブサーバに転送します。

```console
mds1% sudo gfdump.postgresql -d -f dumpfile
mds1% scp dumpfile mds2.example.com:
mds1% scp dumpfile mds3.example.com:
mds1% sudo rm dumpfile
```

スレーブサーバmds2, mds3のそれぞれにおいて以下のようにスレーブメタデータサーバの構築を行います。まず、スレーブメタデータサーバにおいてメタデータサーバ用ユーザとファイルシステムノード用ユーザをローカルシステムに作成します。このユーザ名は変更できません。

```console
mds[23]% sudo useradd -m _gfarmmd
mds[23]% sudo useradd -m _gfarmfs
```

TLS通信を利用する場合メタデータサーバの証明書と秘密鍵を`/etc/pki/tls/certs/gfmd.crt`と`/etc/pki/tls/private/gfmd.key`に設置します。秘密鍵はrootの所有でパーミッションは0600とします。CAの証明書を`/etc/pki/tls/certs/gfarm`ディレクトリにファイル名を`$HASH.0`として設置します。

共有鍵方式を用いる場合は`_gfarmmd`と`_gfarmfs`の共有鍵`$HOME/.gfarm_shared_key`をメタデータサーバからコピーします。所有者がそれぞれ`_gfarmmd`と`_gfarmfs`となっていること、およびパーミッションが0600であることを確認してください。

Gfarmの構築をマスターメタデータサーバと同様に行います。

```console
mds[23]% sudo config-gfarm -N -A $USER -r -X -d md5
```

`config-gfarm`のオプションはメタデータサーバを構築したときと同じオプションを指定します。
-Nオプションはconfig-gfarm実行時にサーバ起動を行わないオプションです。
<!-- >
次に、マスターと同様に有効にする認証方式を`gfmd.conf`に追加します。

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
TLS通信を用いる場合で、gfsdのサービス証明書（DN=gfsd/host.domainのようにDNにgfsd/がついたもの）を用いないで、サーバ証明書を用いる場合は以下の2行を削除します。

```text
spool_server_cred_type host
spool_server_cred_service gfsd
```
< -->
マスターのときと異なり、バックエンドデータベースだけ起動します。

```console
mds[23]% sudo systemctl start gfarm-pgsql
mds[23]% sudo systemctl enable gfarm-pgsql
```

メタデータのダンプファイルでバックエンドデータベースを更新（リストア）します。リストア後はダンプファイルは不要なので消去します。

```console
mds[23]% sudo gfdump.postgresql -r -f dumpfile
mds[23]% rm dumpfile
```

`gfdump.postgresql -r`でメタデータをリストアするとメタデータサーバが起動されます。システム起動時に自動的に起動するように設定します。

```console
mds[23]% sudo systemctl enable gfmd
```

メタデータサーバから`gfarm2.conf`をコピーして上書きします。

### 動作確認

全てのスレーブサーバで上記の構築を行った後、マスターメタデータサーバの初期ユーザにおいて以下を実行して正しくスレーブメタデータサーバが動作しているか確認できます。

```console
mds1% gfmdhost -l
+ master -     m east         mds1.example.com 601
+ slave  sync  c east         mds2.example.com 601
+ slave  async c west         mds3.example.com 601
```

スレーブメタデータサーバの第一カラムが`+`となっていれば正しく同期されています。`x`あるいは`e`となっている場合は、同期に失敗していますので、最新のダンプファイルを用いてリストアしなおしてください。なお、スレーブメタデータサーバは最新のダンプファイルを用いることにより、いつでも追加することが可能です。

## ファイルシステムノード設定

Gfarmのファイルシステムノードとすることにより、そのサーバのローカルストレージをGfarmファイルシステムに組み込むことができます。

まず、ファイルシステムノード用ユーザをローカルシステムに作成します。このユーザ名は変更できません。

```console
fs% sudo useradd -m _gfarmfs
```

TLS通信を利用する場合ファイルシステムノードのサービス証明書と秘密鍵を`/etc/grid-security/gfsd/gfsdcert.pem`と`/etc/grid-security/gfsd/gfsdkey.pem`に設置します。`/etc/grid-security/gfsd`以下の所有者は`_gfarmfs`とし、秘密鍵のパーミッションは0600とします。サービス証明書は`DN=gfsd/host.domain`のようにDNに`gfsd/`がついたものを用いる必要があります。CAの証明書を`/etc/pki/tls/certs/gfarm`ディレクトリにファイル名を`$HASH.0`として設置します。

共有鍵方式を用いる場合は`_gfarmfs`の共有鍵`$HOME/.gfarm_shared_key`をメタデータサーバからコピーします。所有者が`_gfarmfs`となっていること、パーミッションが0600であることを確認してください。

メタデータサーバから設定ファイル`gfarm2.conf`をコピーし、その後`config-gfsd`でファイルシステムノードの設定を行います。以下はファイルシステムノードfs1.example.comにおいて`/var/spool/gfarm`以下のディレクトリをGfarmファイルシステムに組込む例です。

```console
fs% sudo config-gfsd /var/spool/gfarm
created /var/spool/gfarm
created /etc/systemd/system/gfsd.service
created /etc/unconfig-gfsd.sh
config-gfsd success

Please ask admin_user to register your host by the following command:

/usr/local/bin/gfhost -c -a x86_64-debiantrixie_sid-linux -p 600 -n 22 fs1.example.com

After that, start gfsd by the following command as a root:

systemctl start gfsd.service
```

指定するディレクトリは存在しないディレクトリか空ディレクトリとしてください。このディレクトリ以下は`_gfarmfs`ユーザとローカルシステムの特権ユーザしかアクセスできなくなります。`config-gfsd`の詳細はマニュアルページを参照してください。

表示されたコマンド（`gfhost -c -a ...`）をマスターメタデータサーバの初期ユーザで実行し、ファイルシステムノードの登録を行います。
<!-- >
ファイルシステムノードに戻り、`/etc/systemd/system/gfsd.service`の以下の行を削除します。

```text
Environment=X509_USER_CERT=/etc/grid-security/gfsd/gfsdcert.pem
Environment=X509_USER_KEY=/etc/grid-security/gfsd/gfsdkey.pem
```
< -->
ファイルシステムノードに戻り、ファイルシステムサーバgfsdを起動します。

```console
fs% sudo systemctl start gfsd.service
fs% sudo systemctl enable gfsd.service
```

### 動作確認

全てのファイルシステムノードで上記の構築を行った後、マスターメタデータサーバの初期ユーザにおいて以下を実行して正しくファイルシステムサーバが動作しているか確認できます。

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

全てのファイルシステムノードが表示されていたら正しく設定されています。なお、ファイルシステムノードはいつでも追加可能です。

## ユーザ登録

上記の設定をした後は、マスターメタデータサーバにおいて初期ユーザがメタデータサーバに共有鍵でアクセス可能な状態となっています。そのため、`gfmdhost`と`gfdf`を実行することができましたが、ファイルシステムノードへの認証はまだできません。

ユーザを登録するためにはまずユーザをGfarmに登録し、認証の設定を行う必要があります。初期ユーザは既に登録されていますが、それ以外のユーザについては`gfuser`で登録します。登録はGfarmの管理特権ユーザ（いまは初期ユーザ）で行います。以下は山田太郎さんをtaroユーザで作成し、ホームディレクトリを/home/taroとし、クライアント証明書を/O=Gfarm/OU=Test/OU=CA/CN=taroとする例です。所有者の変更には後述のアクセス特権が必要なため、`gfchown`は`gfsudo`で実行します。

```console
mds1% gfuser -c taro "Taro Yamada" /home/taro /O=Gfarm/OU=Test/OU=CA/CN=taro
mds1% gfmkdir -p /home/taro
mds1% gfsudo gfchown taro /home/taro
```

クライアント証明書を用いて認証する場合はこれで終了です。クライアント証明書は後で変更できますのでまずは""として何も指定しないこともできます。

トークンを利用する場合、トークンから得られるIDを登録します。IDがユーザ名と同じtaroであれば不要ですが、そうではない場合はそのIDを`gfuser -A`で追加します。

```console
mds1% gfuser -A taro SASL $ID
```

トークンのどの属性をIDとするかはcyrus-sasl-xoauth2-idpの設定によります。トークンベースの認証の場合はこれで終了です。

共有鍵を利用する場合は、全ホストにホームディレクトリを作成し、共有鍵を置く必要があります。共有鍵の所有者はローカルユーザでパーミッションは0600となっていることを確認してください。ローカルユーザ名はGfarmのユーザ名と違う名前でも可能ですがその場合はマップファイルを作成する必要があります。

### 管理特権ユーザ

ユーザ作成やファイルシステムノードの登録のような設定変更を行うためには、管理特権ユーザで実行する必要があります。管理特権ユーザは`gfarmadm`グループに所属しているユーザです。グループ管理は`gfgroup`コマンドで行います。詳しくは`gfgroup`のマニュアルページを参照してください。

### アクセス特権ユーザ

設定変更ではなく、ファイルデータの管理を行うためのアクセス特権ユーザもあります。アクセス特権ユーザは`gfarmroot`グループに所属しているユーザです。

一時的にアクセス特権ユーザになってコマンドを実行するための`gfsudo`もあります。`gfsudo`は管理特権ユーザが利用可能です。

## クライアント設定

クライアントで必要な設定は、メタデータサーバの`gfarm2.conf`を同じディレクトリ（導入マニュアルに従ってGfarmを導入した場合は/etc）あるいは`$HOME/.gfarm2rc`にコピーすることです。他には、認証のためのクライアント証明書、アクセストークン、共有鍵のいずれかが必要となります。

## 動作確認

登録したそれぞれのユーザにおいて、まず認証設定を行います。クライアント証明書の場合は`/tmp/x509up_u$UID`あるいは環境変数`X509_USER_PROXY`の示す場所に代理証明書を置きます。トークンは`jwt-agent`によりトークンを取得するか、`/tmp/jwt_user_u$UID/token.jwt`あるいは環境変数`JWT_USER_PATH`の示す場所にトークンを置きます。共有鍵の場合は`$HOME/.gfarm_shared_key`に置きます。

認証設定を完了した後、以下で動作確認ができます。

### メタデータサーバへの認証確認

`gfstatus`コマンドによりメタデータサーバへの認証を確認することができます。

```console
% gfstatus
```

### ファイルシステムノードへの認証確認

`gfhost`コマンドによりファイルシステムノードへの接続が確認できます。まずUDPの疎通を確認します。

```console
% gfhost -lvuU
```

左端の数字はサーバのロードアベレージを示しています。ここが`-.--/-.--/-.--`と表示されている場合はファイルシステムノードのサーバが起動していないことを示し、`x.xx/x.xx/x.xx`と表示される場合はそのノードに接続できないことを示しています。

次にTCPの疎通と認証を確認します。

```console
% gfhost -lvu
```

第2欄の文字は認証結果を示しており、`x`は認証失敗、`-`は疎通失敗を示しています。トークンによる認証が成功している場合は`A`または`a`となります。詳細は[`gfhost`のmanページ](https://oss-tsukuba.org/share/doc/gfarm/html/ja/ref/man1/gfhost.1.html)を参照してください。

## 簡単な利用方法

マウントして利用するほか、ファイル複製の設定、多数ファイルの高速コピー、高速アーカイブなどが可能です。

### マウント

Gfarm ファイルシステムは`gfarm2fs`でマウントすることができます。

```console
% mkdir /tmp/foo
% gfarm2fs /tmp/foo
```

マウント後は、通常のコマンドでファイル操作を行うことが可能です。アンマウントは`fusermount3`コマンド（FUSE 2の場合は`fusermount`コマンド）で行います。

```console
% fusermount3 -u /tmp/foo
```

詳細は`gfarm2fs`に含まれるドキュメントを参照してください。

### ファイル複製の設定

`gfncopy`コマンドでファイル複製の数を設定することができます。以下は`/`ディレクトリ以下の全ファイルについてファイル複製を2つ保持するための設定です。`/`の所有者は`gfarmadm`のため、管理特権ユーザが`gfsudo`で実行します。

```console
% gfsudo gfncopy -s 2 /
```

ファイルが格納されているファイルシステムノードは`gfwhere`コマンドで知ることができます。

```console
% gfwhere test
linux-1.example.com linux-2.example.com
```

この例では`linux-1`と`linux-2`にファイルが格納されています。

### 高速コピーコマンド - gfpcopy

大量の（小さい）ファイルをコピーするためのコマンドです。詳細は`gfpcopy`のマニュアルページを参照してください。

### 高速アーカイブコマンド - gfptar

大量の（小さい）ファイルを高速にGfarmに保存するために使用するコマンドです。詳細は`gfptar`のマニュアルページを参照してください。

## バージョンアップの手順

Gfarmを更新するときは、メタデータサーバ、ファイルシステムノード、クライアントの順に更新します。

### メタデータサーバの更新

スレーブメタデータサーバが動作している場合は運用を停止することなくバージョンアップが可能です。まずスレーブメタデータサーバを停止させ、上書きインストールでバージョンアップを行います。バックエンドのPostgreSQLは起動したままで問題ありません。上書きインストール後、`config-gfarm-update --update`を実行してから、メタデータサーバを起動させます。

```console
mds2% sudo systemctl stop gfmd
# Gfarmを上書きインストール
mds2% sudo config-gfarm-update --update
mds2% sudo systemctl start gfmd
```

`gfmdhost`でスレーブメタデータサーバが同期されたことを確認し、マスターからスレーブにメタデータサーバをフェイルオーバします。

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

旧マスターメタデータサーバを更新します。マスターではなくなったため、`gfmd-slave`によりスレーブとして起動します。

```console
# Gfarmを上書きインストール
mds1% sudo config-gfarm-update --update
mds1% sudo systemctl start gfmd-slave
mds1% gfmdhost -l
+ slave  sync  c east         mds1.example.com 601
+ master -     m east         mds2.example.com 601
+ slave  async c west         mds3.example.com 601
```

注1:  
マスターメタデータサーバがmds2に移ったままですがこのまま運用を続けても問題ありませんし、mds1にマスターを戻しても問題ありません。非同期スレーブメタデータサーバの更新はメタデータサーバを停止させ、上書きインストールでバージョンアップを行います。

注2:  
サービス起動・停止スクリプトを用いるシステムでは、
上記 `systemctl start gfmd-slave` の代わりに、
root で以下を実行します。
```console
$RC_DIR/gfmd slavestart
```


```console
mds3% sudo systemctl stop gfmd
# Gfarmを上書きインストール
mds3% sudo config-gfarm-update --update
mds3% sudo systemctl start gfmd
```

`gfmdhost`でスレーブメタデータサーバが同期されたことを確認してください。

### ファイルシステムノードの更新

ファイルシステムノードについては、ファイル複製を作成していれば、運用を続けながら一台ずつ更新することが可能です。ファイルシステムノードのサーバを停止させ、上書きインストールでバージョンアップを行います。

```console
fs% sudo systemctl stop gfsd
# Gfarmを上書きインストール
fs% sudo systemctl start gfsd
```

### クライアントの更新

各ユーザがマウントしているGfarmファイルシステムをアンマウントし、Gfarmおよびgfarm2fsを上書きでインストールします。
