* Abstract

  gfs.h/gfs_misc.h の公開関数および公開関数の構成要素となる内部関数のうち、
  gfmd へのアクセスを含む関数について、 gfmd がフェイルオーバした後でも継続
  して実行できることを確認する。

* Condition

  - gfmd が冗長化構成である。
  - gfsd が 2 つ以上構成されている。
  - 試験に使用する gfarm ユーザが /tmp に対して書き込み権限を持つ。

* Set up / Clean up

  - 自動テスト(test-all.sh auto)を実行する場合は、gfmd-failover-local.sh に
    gfmd をフェイルオーバするスクリプトを記述すること。

    # 具体的に gfmd フェイルオーバするスクリプトは環境依存であるため、
    # テストケースには含まれていない。テスト環境ごとに記述しなければならない。

    簡易的なテストを行うには gfmd-failover-local.sample.sh の内容を参考に
    して、ローカルの gfmd を再起動するスクリプトを記述すれば良い。
    gfmd を再起動することで、フェイルオーバに近い効果を得ることができる。

    gfmd フェイルオーバの流れは次のとおり。

    1. master gfmd を停止
    2. slave gfmd に対して SIGUSR1 を発行。

    以上の処理だけでは一度のフェイルオーバで終わってしまうため、テスト項目を連続
    実行するためには、1. の前に元 master gfmd を slave として起動しなければ
    ならない。

    起動開始〜起動完了までの間にクライアント(テストスクリプト)が
    アクセスしてきても、クライアント側は connection resuse を検知して
    リトライするので、sleep等を入れる必要はない。

    gfmd を gfservice(1) を使用してフェイルオーバさせるには
    gfmd-failover-local-gfservice.sh が利用できる。

    gfservice の設定ファイルである gfservice.conf をテストに用いる
    Gfarm環境に合せて用意し、~/.gfserviceとして保存するか、環境変数
    GFSERVICE_CONF に gfservice.conf のパスを設定して、テストを実行す
    る。gfservice および gfservice.conf についてはそれぞれmanを参照の
    こと。

    gfmd-failover-local-gfservice.sh では master 1台、slave 2台でGfarm
    環境が構築されていることを前提にし、gfmd1 と gfmd2 を交互にフェイ
    ルオーバさせる。 gfmd-failover-local-gfservice.sh では"master"およ
    び"slave"というファイルにそれぞれ、現在の master gfmdと slave
    gfmd を保存する。 "master"ファイルが存在しない場合、gfmd1をマスタ
    としてフェイルオーバを実行する。

    スレーブの台数が上記とは異なる場合、メタデータのバックアップ/リス
    トア処理を台数に合せて gfmd-failover-local-gfservice.shを修正する
    必要がある。

  - テスト用データの setup/cleanup は test-all.sh 内で実行されるため、
    test-all.shを実行する際は 呼び出す必要はない。

  - 個別に手動で実行する場合は、以下のスクリプトを実行すること。
    - 試験前: setup.sh を実行する。
    - 試験後: cleanup.sh を実行する。

* Procedure - 自動テスト

  次のコマンドを実行する。

  $ ./test-all.sh auto

* Procedure - 手動テスト

  次のコマンドを実行する。

  $ ./test-all.sh

  各コマンド実行中に

    *** Push Enter Key ***

  と表示されたら、別のシェルで手動で gfmd をフェイルオーバする。
  続いて元のシェルで Enter Key を入力するとテストが継続する。

* Result

  - failed-list

    失敗したテスト種別はファイル failed-list に保存される。

  - log

    テストプログラムの出力は log に保存される。(手動テスト時はstdout)

* Note

  - テスト種別を指定して個別に実行するには、
    test-all.sh のかわりに、"test-launch.sh [テスト種別]" を実行する。

  - テストを追加するときは、test-list に新しいテスト種別を追加する。

  - test-all.sh に環境変数 SLEEP を渡すと、各テスト実行後に SLEEP 秒間だけ
    停止する。これはフェイルオーバ後に gfsd が gfmd へ接続するまで待機する
    する効果があるが、gfm_client_connect() のリトライ処理が実装されたこと
    により不要になった。

* Test Items

  test-launch.sh の引数に渡すテスト種別ごとのテスト対象一覧。

  gfmdへアクセスする公開関数のうち、内部で以下のテスト対象関数によってだけ
  gfmdへアクセスしているもの、またはテスト対象関数と大部分のロジックを共有
  しているものについては、テスト対象から除外している。
  除外された公開関数については gfs_pio_failover_test.c の test_infos の定義内
  に記述されたコメントを参照のこと。

 - basic

  realpath           ... gfs_realpath()
  rename             ... gfs_rename()
  statfs             ... gfs_statfs()
  statfsnode         ... gfs_statfsnode()
  chmod              ... gfs_chmod()
  lchmod             ... gfs_lchmod()
  chown              ... gfs_chown()
  lchown             ... gfs_lchown()
  readlink           ... gfs_readlink()
  stat               ... gfs_stat()
  lstat              ... gfs_lstat()
  fstat              ... gfs_fstat()
  utimes             ... gfs_utimes()
  lutimes            ... gfs_lutimes()
  remove             ... gfs_remove()
  unlink             ... gfs_unlink()
  link               ... gfs_link()
  symlink            ... gfs_symlink()
  mkdir              ... gfs_rmdir()
  opendir            ... gfs_opendir()
  opendirplus        ... gfs_opendirplus()
  opendirplusxattr   ... gfs_opendirplusxattr() *1
  closedir           ... gfs_closedir()
  closedirplus       ... gfs_closedirplus() *1
  closedirplusxattr  ... gfs_closedirplusxattr()
  readdir            ... gfs_readdir()
  readdir2           ... gfs_readdir(), 他の操作によりすでにfailover処理が行わ
                         れていて、GFS_Dirが古いgfm_connectionを保有している
                         ケース
  readdirplus        ... gfs_readdirplus()
  readdirplusxattr   ... gfs_readdirplusxattr() *1
  seekdir            ... gfs_seekdir()
  seekdirplusxattr   ... gfs_seekdirplusxattr() *1

  *1 gfs_*dirxattrplus() は公開関数ではないが、gfs_opendir_caching() を
     通じて gfarm2fs から呼ばれるためテスト対象としている。

 - gfs_pio

  sched-read         ... scheduling前, gfs_pio_read()
  sched-create-write ... scheduling前, gfs_pio_create(), gfs_pio_write()
  sched-open-write   ... scheduling前, gfs_pio_open(), gfs_pio_write()
  close              ... scheduling前, gfs_pio_close()
  close-open         ... gfm_connection error後のcloseにより
                         複数のGFS_Fileに異なるgfm_connectionが設定されるケース,
                         scheduling後が混在
  close-open2        ... gfm_connection error後のcloseにより
                         複数のGFS_Fileに異なるgfm_connectionが設定されるケース,
                         scheduling前
  read               ... scheduling前/後, gfs_pio_read()
  read-stat          ... scheduling前/後, gfs_pio_read(), gfs_pio_stat()
  getc               ... scheduling前/後, gfs_pio_getc(), gfs_pio_ungetc()
  seek               ... scheduling前/後, buffer dirtyではない状態,
                         gfs_pio_seek()
  seek-dirty         ... scheduling前/後, buffer dirtyの状態, gfs_pio_seek()
  write              ... scheduling前/後, gfs_pio_write()
  write-stat         ... scheduling前/後, gfs_pio_write(), gfs_pio_stat()
  putc               ... scheduling前/後, gfs_pio_putc()
  truncate           ... scheduling前 buffer dirtyではない状態,
                         scheduling後 buffer dirtyの状態, gfs_pio_truncate()
  flush              ... scheduling前 buffer dirtyではない状態
                         scheduling後 buffer dirtyの状態, gfs_pio_flush()
  sync               ... scheduling前 buffer dirtyではない状態
                         scheduling後 buffer dirtyの状態, gfs_pio_sync()
  datasync           ... scheduling前 buffer dirtyではない状態
                         scheduling後 buffer dirtyの状態, gfs_pio_datasync()
 - xattr/xmlattr

  fsetxattr          ... gfs_fsetxattr()
  getxattr           ... gfs_getxattr()
  lgetxattr          ... gfs_lgetxattr()
  getattrplus        ... gfs_getattrplus()
  lgetattrplus       ... gfs_lgetattrplus()
  setxattr           ... gfs_setxattr()
  lsetxattr          ... gfs_lsetxattr()
  removexattr        ... gfs_removexattr()
  lremovexattr       ... gfs_lremovexattr()
  fgetxattr          ... gfs_fgetxattr()
  fsetxattr          ... gfs_fsetxattr()
  fremovexattr       ... gfs_fremovexattr()
  listxattr          ... gfs_listxattr()
  llistxattr         ... gfs_llistxattr()
  setxmlattr         ... gfs_setxmlattr()
  lsetxmlattr        ... gfs_lsetxmlattr()
  getxmlattr         ... gfs_getxmlattr()
  lgetxmlattr        ... gfs_lgetxmlattr()
  listxmlattr        ... gfs_listxmlattr()
  llistxmlattr       ... gfs_llistxmlattr()
  removexmlattr      ... gfs_removexmlattr()
  lremovexmlattr     ... gfs_lremovexmlattr()
  findxmlattr        ... gfs_findxmlattr()
  getxmlent          ... gfs_getxmlent()
  closexmlattr       ... gfs_closexmlattr()

 - scheduling

  shhosts            ... gfarm_schedule_hosts()
  shhosts-domainall  ... gfarm_schedule_hosts_domain_all()
  shhosts-domainfile ... gfarm_schedule_hosts_domain_by_file()

 - replication

  rep-info           ... gfs_replica_info_by_name()
  rep-list           ... gfs_replica_list_by_name()
  rep-to             ... gfs_replicate_to()
  rep-fromto         ... gfs_replicate_from_to()
  rep-toreq          ... gfs_replicate_file_to_request()
  rep-fromtoreq      ... gfs_replicate_file_from_to_request()
  rep-remove         ... gfs_replica_remove_by_file()
  migrate-to         ... gfs_migrate_to()
  migrate-fromto     ... gfs_migrate_fromto()

