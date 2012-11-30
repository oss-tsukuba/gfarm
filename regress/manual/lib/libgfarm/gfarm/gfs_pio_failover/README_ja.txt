* Abstract

  gfs_pio_open() および gfs_pio_create() の後、gfmd がフェイルオーバした場合、
  ファイルアクセス系の関数を実行すると、フェイルオーバ後も継続してファイルア
  クセスできることを確認する。

* Condition

  - gfmd が冗長化構成である。
  - 試験に使用する gfarmユーザが /tmp に対して書き込み権限を持つ。

* Set up / Clean up

  - 試験前: setup.sh を実行する。
  - 試験後: cleanup.sh を実行する。

* Test Item

  テスト実行コマンドごとのテスト対象一覧

  test-sched-read.sh         ... scheduling前, gfs_pio_read()

  test-sched-create-write.sh ... scheduling前,
                                 gfs_pio_create(), gfs_pio_write()

  test-sched-open-write.sh   ... scheduling前,
                                 gfs_pio_open(), gfs_pio_write()

  test-read.sh               ... scheduling前/後, gfs_pio_read()

  test-read-stat.sh          ... scheduling前/後,
                                 gfs_pio_read(), gfs_pio_stat()

  test-getc.sh               ... scheduling前/後,
                                 gfs_pio_getc(), gfs_pio_ungetc()

  test-seek.sh               ... scheduling前/後, buffer dirtyではない状態,
                                 gfs_pio_seek()

  test-seek-dirty.sh         ... scheduling前/後, buffer dirtyの状態,
                                 gfs_pio_seek()

  test-write.sh              ... scheduling前/後, gfs_pio_write()

  test-write-stat.sh         ... scheduling前/後, gfs_pio_write(), gfs_pio_stat()

  test-putc.sh               ... scheduling前/後, gfs_pio_putc()

  test-truncate.sh           ... scheduling前 buffer dirtyではない状態
                                 scheduling後 buffer dirtyの状態,
								 gfs_pio_truncate()

  test-flush.sh              ... scheduling前 buffer dirtyではない状態
                                 scheduling後 buffer dirtyの状態,
								 gfs_pio_flush()

  test-sync.sh               ... scheduling前 buffer dirtyではない状態
                                 scheduling後 buffer dirtyの状態,
								 gfs_pio_sync()

  test-datasync.sh           ... scheduling前 buffer dirtyではない状態
                                 scheduling後 buffer dirtyの状態,
								 gfs_pio_datasync()

* Procedure

  次のコマンドを実行する。

  $ ./test-all.sh

  各コマンド実行中に

    *** wait for SIGUSR2 to continue ***

  と表示されたら、別のシェルから root ユーザで次のコマンドを実行して
  gfmd をフェイルオーバする。

  (master gfmd のホストにおいて)
  # ./gfmd-kill.sh 
  (slave gfmd のホストにおいて)
  # ./gfmd-tomaster.sh

  続いて次のコマンドでコマンドの実行を継続する。
  $ ./resume.sh

  コマンドの最後に "OK" が出力されたら試験成功、それ以外の場合は失敗。

  << 備考 >>

  1. test-all.sh のかわりに、test-all.sh で実行される test-*.sh を個別に
     実行しても良い。

  2. test-all.sh に環境変数 SLEEP を渡すと、各テスト実行後に SLEEP 秒間だけ
     停止する。これはフェイルオーバ後に gfsd が gfmd へ接続するまで待機する
	 する効果がある。

