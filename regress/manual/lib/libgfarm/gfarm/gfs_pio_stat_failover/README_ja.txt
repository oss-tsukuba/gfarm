* Abstract

  gfmd がフェイルオーバした後、gfs_pio_stat() が実行されるとすべてのオープン中
  のファイルに関して再接続処理が行われ、フェイルオーバ後もファイルへアクセスで
  きることを確認する。

* Condition

  - gfmd が冗長化構成である。
  - 試験に使用する gfarmユーザが /tmp に対して書き込み権限を持つ。

* Set up / Clean up

  - 試験前: setup.sh を実行する。
  - 試験後: clean.sh を実行する。

* Test Item

  test-1.sh ... gfs_pio_read()
  test-2.sh ... gfs_pio_write()

* Procedure

  次のコマンドを実行する。

  $ ./test-1.sh
  $ ./test-2.sh

  コマンド実行中に

   *** Please failover gfmd manually and push Enter Key ***

  と表示されたら、別のシェルから root ユーザで次のコマンドを実行して
  gfmd をフェイルオーバしてください。

  (master gfmd のホストにおいて)
  # ./gfmd-kill.sh 
  (slave gfmd のホストにおいて)
  # ./gfmd-tomaster.sh

  次に元のシェルでEnterキーを入力して、テストを続行してください。
  コマンドの最後に "OK" が出力されたら試験成功、それ以外の場合は失敗。

