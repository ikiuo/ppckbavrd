# 玄箱 初代/HG 用 AVR 監視デーモン

ビルドとインストール

```
$ make
$ sudo make install
```

<code>/etc/rc?.d</code>への登録

```
$ sudo /usr/sbin/update-rc.d ppckbavrd defaults 95
```

## <code>/etc/ppckbavrd</code> ディレクトリ

イベント用スクリプトを配置するディレクトリです。

- <code>allevent</code>は全イベントで呼び出される
- <code>event-XX</code>はバイト コード (XX:16進数2桁)に応じて呼び出される

引数は<code>ポート コード 時刻 経過時間</code>で呼び出されます。

- <code>ポート</code>はデバイス ファイル
- <code>コード</code>は16進数2桁のバイト コード
- <code>時刻</code>は UNIX 時間
- <code>経過時間</code>は<code>コード</code>が
  - <code>20</code>のときは<code>21</code>からの経過時間
  - <code>22</code>のときは<code>23</code>からの経過時間
  - 上記以外は同一コードからの経過時間

### <code>allevent</code>スクリプト

- 電源スイッチ
  - 長押し約3〜5秒でシャットダウン
  - 長押し約6秒以上でリブート
- 初期化スイッチ
  - 長押し約2秒以上ので EM モードを設定してリブート

## debian用パッケージ

パッケージのビルドに必要なツールのインストール

```
$ sudo apt-get install build-essential debhelper dh-make devscripts fakeroot
```

パッケージのビルド

```
$ make deb-pkg
```

1つ上のディレクトリに<code>ppckbavrd\_0.1.0-1\_powerpc.deb</code>などが作られます。

## ビルド済みパッケージ

- [Release-0.1.0](https://github.com/ikiuo/ppckbavrd/releases/tag/Release-0.1.0)
