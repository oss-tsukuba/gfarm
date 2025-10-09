# Gfarm設定マニュアル

## はじめに

本マニュアルはGfarmが導入されていることを前提として、Gfarmの構築を行うためのものです。Gfarmを構築するために、まずファイルシステムメタデータを管理するメタデータサーバの構築を行います。その後、ファイルシステムノード、クライアントの構築を行います。

## メタデータサーバ設定

メタデータサーバは複数のサーバでマスタースレーブ型の冗長構成をとることができます。以下、ローカルサーバの特権ユーザで実行する必要のあるコマンドはsudoで実行しています。

### マスターメタデータサーバの設定

まず、マスターメタデータサーバにおいてメタデータサーバ用ユーザとファイルシステムノード用ユーザをローカルシステムに作成します。このユーザ名は変更できません。

```console
% sudo useradd -m _gfarmmd
% sudo useradd -m _gfarmfs
```

TLS通信を利用する場合メタデータサーバの証明書と秘密鍵を`/etc/pki/tls/certs/gfmd.crt`と`/etc/pki/tls/private/gfmd.key`に設置します。秘密鍵はrootの所有でパミッションは0600とします。CAの証明書を`/etc/pki/tls/certs/gfarm`ディレクトリにファイル名を`$HASH.0`として設置します。`$HASH`は`openssl x509 -hash -noout -in $CRT`で計算できます。`$CRT`はCAの証明書です。

証明書を設置しない場合、全ての通信は平文となります。認証はGfarm独自の共有鍵方式となり、それぞれのユーザにおいて共有鍵を作成する必要があります。以下では1年間（=31,536,000秒）有効な共有鍵を作成しています。

```console
% sudo -u _gfarmmd gfkey -f -p 31536000
% sudo -u _gfarmfs gfkey -f -p 31536000
```

共有鍵は`$HOME/.gfarm_shared_key`に作成され、`_gfarmmd`の共有鍵は全てのメタデータサーバ、`_gfarmfs`の共有鍵は全てのメタデータサーバ、ファイルシステムノードに同じ所有者と同じパミッションでコピーしておく必要があります。また、有効期限が過ぎる前に更新が必要です。

Gfarmファイルシステムを構築します。

```console
% sudo config-gfarm -N -A $USER -r -X -d md5
```

`config-gfarm`によりGfarmに必要な設定ファイル（gfarm2.confとgfmd.conf）およびバックエンドデータベースであるPostgreSQLの設定がなされ、メタデータサーバgfmdとPostgreSQLサーバの起動スクリプトが設置されます。設定ファイルはGfarmの構築時に`--sysconfdir`オプションにより指定したディレクトリ、指定していない場合は`--prefix`で指定したディレクトリを`$PREFIX`とすると`$PREFIX/etc`となります。`$PREFIX`のデフォルトは/usr/localです。

-Nオプションはconfig-gfarm実行時にサーバ起動を行わないオプションです。-AオプションでGfarmファイルシステムにおける初期ユーザを指定します。`$USER`ではマスターメタデータサーバの一般ユーザアカウントを指定してください。-rオプションでメタデータサーバの冗長構成用の設定を行います。-Xオプションで拡張XML属性を有効にします。-dオプションでデータ完全性を保証するための設定を有効とし、そのダイジェストのアルゴリズムを指定します。`config-gfarm`の詳細はマニュアルページを参照してください。

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
<!-- >
TLS通信を用いる場合で、gfsdのサービス証明書（DN=gfsd/host.domainのようにDNにgfsd/がついたもの）を用いないで、サーバ証明書を用いる場合は以下の2行を削除します。

```text
spool_server_cred_type host
spool_server_cred_service gfsd
```
< -->
バックエンドデータベースとメタデータサーバを起動し、またシステム起動時に自動的に起動するように設定します。

```console
% sudo systemctl start gfarm-pgsql
% sudo systemctl enable gfarm-pgsql
% sudo systemctl start gfmd
% sudo systemctl enable gfmd
```

次に、`gfarm2.conf`を更新して、冗長構成をとるメタデータサーバのリストと有効にする認証方式を追加します。以下は冗長構成のメタデータサーバのFQDNをmds1.example.com, mds2.example.com, mds3.example.comとしたときの例です。

```text
metadb_server_list mds1.example.com:601 mds2.example.com:601 mds3.example.com:601
auth enable tls_client_certificate *
auth enable tls_sharedsecret *
auth enable sasl *
auth enable sasl_auth *
auth enable kerberos *
auth enable kerberos_auth *
sasl_mechanisms XOAUTH2
```

メタデータサーバの指定における`:601`は使用するTCPポート番号を表し、デフォルトの601番を用いる場合は省略可能です。
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
% gfmdhost -m mds1.example.com -C east
% gfmdhost -c mds2.example.com -C east
% gfmdhost -c mds3.example.com -C west
```

### スレーブメタデータサーバの設定

これからスレーブメタデータサーバの設定を実施します。そのためにまずマスターメタデータサーバのメタデータのダンプを取り、それぞれのスレーブサーバに転送します。

```console
% sudo gfdump.postgresql -d -f dumpfile
% scp dumpfile mds2.example.com:
% scp dumpfile mds3.example.com:
% sudo rm dumpfile
```

スレーブサーバmds2, mds3のそれぞれにおいて以下のようにスレーブメタデータサーバの構築を行います。まず、スレーブメタデータサーバにおいてメタデータサーバ用ユーザとファイルシステムノード用ユーザをローカルシステムに作成します。このユーザ名は変更できません。

```console
% sudo useradd -m _gfarmmd
% sudo useradd -m _gfarmfs
```

TLS通信を利用する場合メタデータサーバの証明書と秘密鍵を`/etc/pki/tls/certs/gfmd.crt`と`/etc/pki/tls/private/gfmd.key`に設置します。秘密鍵はrootの所有でパミッションは0600とします。CAの証明書を`/etc/pki/tls/certs/gfarm`ディレクトリにファイル名を`$HASH.0`として設置します。

共有鍵方式を用いる場合は`_gfarmmd`と`_gfarmfs`の共有鍵`$HOME/.gfarm_shared_key`をメタデータサーバからコピーします。所有者がそれぞれ`_gfarmmd`と`_gfarmfs`となっていること、およびパミッションが0600であることを確認してください。

Gfarmの構築をマスターメタデータサーバと同様に行います。

```console
% sudo config-gfarm -N -A $USER -r -X -d md5
```

`config-gfarm`のオプションはメタデータサーバを構築したときと同じオプションを指定します。次に、マスターと同様に有効にする認証方式を`gfmd.conf`に追加します。

```text
auth enable tls_client_certificate *
auth enable tls_sharedsecret *
auth enable sasl *
auth enable sasl_auth *
auth enable kerberos *
auth enable kerberos_auth *
sasl_mechanisms XOAUTH2
```
<!-- >
TLS通信を用いる場合で、gfsdのサービス証明書（DN=gfsd/host.domainのようにDNにgfsd/がついたもの）を用いないで、サーバ証明書を用いる場合は以下の2行を削除します。

```text
spool_server_cred_type host
spool_server_cred_service gfsd
```
< -->
マスターのときと異なり、バックエンドデータベースだけ起動します。

```console
% sudo systemctl start gfarm-pgsql
% sudo systemctl enable gfarm-pgsql
```

メタデータのダンプファイルでバックエンドデータベースを更新（リストア）します。リストア後はダンプファイルは不要なので消去します。

```console
% sudo gfdump.postgresql -r -f dumpfile
% rm dumpfile
```

`gfdump.postgresql -r`でメタデータをリストアするとメタデータサーバが起動されます。システム起動時に自動的に起動するように設定します。

```console
% sudo systemctl enable gfmd
```

メタデータサーバから`gfarm2.conf`をコピーして上書きします。

### 動作確認

全てのスレーブサーバで上記の構築を行った後、マスターメタデータサーバの初期ユーザにおいて以下を実行して正しくスレーブメタデータサーバが動作しているか確認できます。

```console
% gfmdhost -l
+ master -     m east         mds1.example.com 601
+ slave  sync  c east         mds2.example.com 601
+ slave  async c west         mds3.example.com 601
```

スレーブメタデータサーバの第一カラムが`+`となっていれば正しく同期されています。なお、スレーブメタデータサーバは、最新のダンプファイルを用いることにより、いつでも追加することが可能です。

## ファイルシステムノード設定

Gfarmのファイルシステムノードとすることにより、そのサーバのローカルストレージをGfarmファイルシステムに組み込むことができます。

まず、ファイルシステムノード用ユーザをローカルシステムに作成します。このユーザ名は変更できません。

```console
% sudo useradd -m _gfarmfs
```

TLS通信を利用する場合ファイルシステムノードのサービス証明書と秘密鍵を`/etc/grid-security/gfsd/gfsdcert.pem`と`/etc/grid-security/gfsd/gfsdkey.pem`に設置します。`/etc/grid-security/gfsd`以下の所有者は`_gfarmfs`とし、秘密鍵のパミッションは0600とします。サービス証明書は`DN=gfsd/host.domain`のようにDNに`gfsd/`がついたものを用いる必要があります。CAの証明書を`/etc/pki/tls/certs/gfarm`ディレクトリにファイル名を`$HASH.0`として設置します。

共有鍵方式を用いる場合は`_gfarmfs`の共有鍵`$HOME/.gfarm_shared_key`をメタデータサーバからコピーします。所有者が`_gfarmfs`となっていること、パミッションが0600であることを確認してください。

メタデータサーバから設定ファイル`gfarm2.conf`をコピーし、その後`config-gfsd`でファイルシステムノードの設定を行います。以下はファイルシステムノードfs1.exemple.comにおいて`/var/spool/gfarm`以下のディレクトリをGfarmファイルシステムに組込む例です。

```console
% sudo config-gfsd /var/spool/gfarm
created /var/spool/gfarm
created /etc/systemd/system/gfsd.service
created /usr/local/etc/unconfig-gfsd.sh
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
% sudo systemctl start gfsd.service
% sudo systemctl enable gfsd.service
```

### 動作確認

全てのファイルシステムノードで上記の構築を行った後、マスターメタデータサーバの初期ユーザにおいて以下を実行して正しくファイルシステムサーバが動作しているか確認できます。

```console
% gfdf
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

ユーザを登録するためにはまずユーザをGfarmに登録し、認証の設定を行う必要があります。初期ユーザは既に登録されていますが、それ以外のユーザについては`gfuser`で登録します。登録はGfarmの管理特権ユーザ（いまは初期ユーザ）で行います。以下は山田太郎さんをtaroユーザで作成し、ホームディレクトリを/home/taroとし、クライアント証明書を/O=Gfarm/OU=Test/OU=CA/CN=taroとする例です。

```console
% gfuser -c taro "Taro Yamada" /home/taro /O=Gfarm/OU=Test/OU=CA/CN=taro
% gfmkdir -p /home/taro
% gfchown taro /home/taro
```

クライアント証明書を用いて認証する場合はこれで終了です。クライアント証明書は後で変更できますのでまずは""として何も指定しないこともできます。

トークンを利用する場合、トークンから得られるIDを登録します。IDがユーザ名と同じtaroであれば不要ですが、そうではない場合はそのIDを`gfuser -A`で追加します。

```console
gfuser -A taro SASL $ID
```

トークンのどの属性をIDとするかはcyrus-sasl-xoauth2-idpの設定によります。トークンベースの認証の場合はこれで終了です。

共有鍵を利用する場合は、全ホストにホームディレクトリを作成し、共有鍵を置く必要があります。共有鍵の所有者はローカルユーザでパミッションは0600となっていることを確認してください。ローカルユーザ名はGfarmのユーザ名と違う名前でも可能ですがその場合はマップファイルを作成する必要があります。

### 管理特権ユーザ

ユーザ作成やファイルシステムノードの登録のような設定変更を行うためには、管理特権ユーザで実行する必要があります。管理特権ユーザは`gfarmadm`グループに所属しているユーザです。グループ管理は`gfgroup`コマンドで行います。詳しくは`gfgroup`のマニュアルページを参照してください。

### アクセス特権ユーザ

設定変更ではなく、ファイルデータの管理を行うためのアクセス特権ユーザもあります。アクセス特権ユーザは`gfarmroot`グループに所属しているユーザです。

一時的にアクセス特権ユーザになってコマンドを実行するための`gfsudo`もあります。`gfsudo`は管理特権ユーザが利用可能です。

## クライアント設定

クライアントで必要な設定は`gfarm2.conf`を`$PREFIX/etc/gfarm2.conf`あるいは`$HOME/.gfarm2rc`に置くことです。他には、認証のためのクライアント証明書、アクセストークン、共有鍵が必要となります。

## 動作確認

登録したそれぞれのユーザにおいて、まず認証設定を行います。クライアント証明書の場合は`/tmp/x509up_u$UID`あるいは環境変数`X509_USER_PROXY`の示す場所に代理証明書を置きます。トークンは`jwt-agent`によりトークンを取得するか、`/tmp/jwt_user_u$UID/token.jwt`あるいは環境変数`JWT_USER_PASS`の示す場所にトークンを置きます。共有鍵の場合は`$HOME/.gfarm_shared_key`に置きます。

認証設定を完了した後、以下で動作確認ができます。

### メタデータサーバへの認証確認

`gfstatus`コマンドによりメタデータサーバへの認証を確認することができます。

```console
$ gfstatus
```

### ファイルシステムノードへの認証確認

`gfhost`コマンドによりファイルシステムノードへの接続が確認できます。まずUDPの疎通を確認します。

```console
$ gfhost -lvuU
```

左端の数字はサーバのロードアベレージを示しています。ここが`-.--/-.--/-.--`と表示されている場合はファイルシステムノードのサーバが起動していないことを示し、`x.xx/x.xx/x.xx`と表示される場合はそのノードに接続できないことを示しています。

次にTCPの疎通と認証を確認します。

```console
$ gfhost -lvu
```

第2欄の文字は認証結果を示しており、`x`は認証失敗、`-`は疎通失敗を示しています。トークンによる認証が成功している場合は`A`または`a`となります。詳細は`gfhost`のmanページを参照してください。  
<http://oss-tsukuba.org/gfarm/share/doc/gfarm/html/ja/ref/man1/gfhost.1.html>
