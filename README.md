# tenomi-rover

**日本語** | [English](README.en.md)

TENOMI（ことばのいらない AI ロボット）の構成要素のひとつで、ジェスチャーに従って走る Rover（micro:bit v2 ベースのクローラー）のアプリケーション層です。

Rover は **μT-Kernel 3.0 上で動作する BLE Peripheral** として実装しています。BLE の Nordic UART Service（NUS）で届く走行指令を受け取り、左右のモータを駆動します。

## TENOMI の中での位置づけ

```
[STM32N6570-DK]  μT-Kernel 3.0 ＋ NPU     … ジェスチャー認識
      │  USB シリアル
      ▼
[Bridge]  Raspberry Pi                     … USB シリアル ⇄ BLE の中継
      │  BLE（Nordic UART Service）：JSON 1 行
      ▼
[Rover]  micro:bit v2 ＋ μT-Kernel 3.0     … 走行（本リポジトリ）
      前進・後退・右折・左折・停止
```

Rover は接続相手が何であるかを見ません。NUS 上に届く**改行区切りの JSON だけ**を解釈して走ります。

## ソフトウェアの構成

Rover のファームウェアは、次の 3 つを組み合わせてビルドします。

| 構成要素 | 提供元 | 役割 |
|----------|--------|------|
| μT-Kernel 3.0（micro:bit 版） | パーソナルメディア株式会社（以下 PMC） | リアルタイム OS・デバイス基盤 |
| [benus](https://github.com/koh523/benus) | 別リポジトリ | BLE NUS を μT-Kernel のデバイス API で扱えるようにするドライバ |
| app_rover | 本リポジトリ | JSON の解釈・走行状態の管理・モータ・LED 表示 |

本リポジトリに含まれるのは app_rover だけです。**clone しただけではビルドできません。** PMC の μT-Kernel 3.0 に benus を適用し、そこへ本リポジトリのソースをコピーしてビルドします（[ビルドと書き込み](#ビルドと書き込み)を参照）。

BLE は benus を通して、μT-Kernel 標準のデバイス API で扱います。

```c
dd = tk_opn_dev("blua", TD_UPDATE);   /* SoftDevice 起動・アドバタイズ開始 */
tk_srea_dev(dd, 0, &data, 1, &asize); /* BLE 受信 */
tk_swri_dev(dd, 0, buf, n, &asize);   /* BLE 送信（Notify） */
```

## しくみ

走行制御は、性質の違う 3 つの処理を μT-Kernel のタスクに分けて組み立てています。

| タスク | 優先度 | 役割 |
|--------|--------|------|
| 受信（`task_rx`） | 高 | BLE から届いたバイト列を改行まで組み立て、JSON を解釈する |
| 接続監視（`task_conn`） | 中 | BLE の接続状態を監視し、切断を検知したら直ちに停止する |
| タイマー（`task_timer`） | 低 | 1 秒ごとに走行の残り時間を減らし、時間が来たら自動で停止する |

- 走行状態は STOP / FORWARD / REVERSE の 3 つです
- 走行状態はミューテックスで保護しており、3 つのタスクから同時に操作されても壊れません
- 接続監視を独立したタスクにしているため、受信処理が詰まっていても切断時の停止は遅れません
- 現在の状態は 5×5 LED に表示します（[動作確認](#動作確認)を参照）

## リポジトリの内容

| ファイル | 内容 |
|----------|------|
| `app_rover/app_main.c` | エントリポイント。BLE デバイスのオープンと 3 タスクの生成、受信・接続監視・タイマーの各処理 |
| `app_rover/rover.h` | app_rover 内で共有する型・定数・関数の宣言 |
| `app_rover/rover_json.c` | 改行区切り JSON の解釈と、状態を返す JSON の生成 |
| `app_rover/rover_sm.c` | 走行状態（STOP / FORWARD / REVERSE）の管理と、時間を指定した走行 |
| `app_rover/rover_motor.c` | 左右モータの駆動（PWM と方向ピン、停止時のブレーキ） |
| `app_rover/rover_led.c` | 状態アイコンの定義と表示 |
| `app_rover/ledmatrix.c` / `app_rover/ledmatrix.h` | 5×5 LED マトリックスの GPIO 設定と行走査 |

## 必要なもの

### ハードウェア

| 品目 | 備考 |
|------|------|
| BBC micro:bit **v2**（nRF52833） | **v1 では動作しません** |
| クローラー（左右モータ付きシャーシ） | 下表の配線で接続します |
| 電源（乾電池ボックスなど） | micro:bit とモータへの給電 |
| USB ケーブル | PC と micro:bit をつなぎ、ファームウェアを書き込みます |

### モータの配線（micro:bit v2 エッジコネクタ）

| 側 | DIR（方向） | PWM（速度） |
|----|-------------|-------------|
| 左 | P14 | P13 |
| 右 | P16 | P15 |

- DIR が `0` で前進、`1` で後退します
- 走行指令の値 `-100`〜`100` を、PWM の `0`〜`1023` に対応させています
- 停止時は PWM を止め、4 本のピンをすべて LOW にしてブレーキをかけます

### ビルドと書き込みに使うソフトウェア

| 項目 | 用途 |
|------|------|
| GNU Arm Embedded Toolchain | ビルド |
| make（Windows では xPack Windows Build Tools など） | ビルド |
| Python 3.8 以上 | benus の適用スクリプト |
| pyocd | micro:bit への書き込み |

導入方法は「[micro:bit で μT-Kernel 3.0 を動かそう](https://www.t-engine4u.com/info/mbit/2.html)」第 2 回（開発ツールの準備とコンパイル）に従ってください。以下の手順は、Git Bash 環境での操作を想定しています。

## ビルドと書き込み

PMC の μT-Kernel 3.0 に benus を適用し、そこへ app_rover を組み込んでビルドします。

```
  PMC の μT-Kernel 3.0（mtkernel_3）      ← ビルドのルート
            ＋
  benus（BLE NUS ドライバ・SoftDevice）   ← パッチを当てて同居させる
            ＋
  app_rover（本リポジトリ）
```

作業用のディレクトリには、空の場所を選んでください。

### 手順 1 — μT-Kernel 3.0 を展開する

「[micro:bit で μT-Kernel 3.0 を動かそう](https://www.t-engine4u.com/info/mbit/2.html)」のページから `362_mbit_mtk3.zip` を入手し、展開します。

展開した `mtkernel_3` の下に `kernel/` `lib/` `device/` `include/` `config/` `etc/` `app_sample/` `build_make/` があることを確認してください。以降、このディレクトリを `<mtkernel_3>` と表記します。

### 手順 2 — benus を取得して適用する

benus は `mtkernel_3` の**外側**に clone します（`mtkernel_3` の中で clone しないでください）。

```bash
git clone https://github.com/koh523/benus.git
python benus/patch/apply.py --mtk3 <mtkernel_3>
```

これで BLE ドライバ・SoftDevice・リンカスクリプトが配置され、カーネル側にパッチが当たります。詳しくは benus の README を参照してください。

### 手順 3 — app_rover を配置する

本リポジトリを clone し、clone したディレクトリの `app_rover/` にあるソースを `<mtkernel_3>/app_rover/` へコピーします。

```bash
git clone https://github.com/tenomi-akkabane/tenomi-rover.git

MTK3=<mtkernel_3>
APP_SRC=tenomi-rover/app_rover   # clone したディレクトリの下の app_rover

mkdir -p "$MTK3/app_rover"
cp "$APP_SRC"/*.c "$APP_SRC"/*.h "$MTK3/app_rover/"
```

ビルドの対象を app_rover に切り替えます。`<mtkernel_3>/build_make/makefile` のアプリの指定を次のようにします。

```
APP = app_rover
```

初回は、benus が配置した `app_sample` のビルド用ファイルを流用します。

```bash
mkdir -p "$MTK3/build_make/mtkernel_3/app_rover"
cp "$MTK3/build_make/mtkernel_3/app_sample/subdir.mk" \
   "$MTK3/build_make/mtkernel_3/app_rover/subdir.mk"
sed -i 's/app_sample/app_rover/g' \
   "$MTK3/build_make/mtkernel_3/app_rover/subdir.mk"
```

### 手順 4 — ビルドする

```bash
cd <mtkernel_3>/build_make
make
```

成功すると、同じディレクトリに `mtkernel_3.elf` が生成されます。

### 手順 5 — micro:bit へ書き込む

micro:bit v2 を USB ケーブルで PC に接続します。micro:bit に MakeCode などで作成したプログラムが残っている場合は、先に消去します。

```bash
pyocd erase --mass
```

続けて書き込みます。

```bash
pyocd load -t nrf52 mtkernel_3.elf
```

書き込みに成功すると、5×5 LED に「寝顔」のアイコンが表示されます。

> ソースを修正した場合は、`tenomi-rover/app_rover/` から `<mtkernel_3>/app_rover/` へ**コピーし直してから** `make` してください。シンボリックリンクでは同期されません。

## 動作確認

### 電源を入れる順番

**Rover の電源を先に入れ、その後で Bridge を起動してください。**

1. Rover の電源を入れる → LED に「寝顔」が表示される（起動完了、BLE のアドバタイズを開始）
2. Bridge を起動する → Bridge が Rover を探して自動で接続する
3. 接続が確立すると、LED がチェックマーク（レ点）に変わる

Rover は `micro:bit2_UART` という名前でアドバタイズします。ペアリングの操作は不要です。同時に接続できるのは 1 台だけです。

### LED 表示の一覧

Rover は 5×5 の LED に現在の状態を表示します。

**接続状態**

| 表示 | 意味 |
|------|------|
| 寝顔 | 起動直後。BLE 未接続（アドバタイズ中） |
| ✓（レ点） | BLE の接続が確立した。走行指令を受け付けられる |
| ✕ | BLE の切断を検知した。同時にモータを停止する |

**走行状態**

| 表示 | 意味 |
|------|------|
| □ | 停止中 |
| ↑ | 前進（左右同速） |
| ↓ | 後退（左右同速） |
| ↖ | 前進しながら右へ旋回（左輪が速い） |
| ↗ | 前進しながら左へ旋回（右輪が速い） |
| ↙ | 後退しながら旋回（左輪が速い） |
| ↘ | 後退しながら旋回（右輪が速い） |

旋回時の斜めの矢印は、**Rover と向かい合う操作者から見た向き**で表示します。Rover 自身が右へ曲がるとき、正面に立つ操作者からは左向き（↖）に見えます。

## うまくいかないとき

| 症状 | 確認すること |
|------|--------------|
| ビルドが通らない | benus の適用（手順 2）が完了しているか。`APP = app_rover` を指定したか。`subdir.mk` を用意したか（手順 3） |
| 書き込めない | `pyocd erase --mass` で消去してから `pyocd load` を実行する |
| LED に何も表示されない | 電源が入っているか。書き込みが完了しているか |
| LED が「寝顔」のまま変わらない | Bridge が起動しているか。Bridge 側で指定している接続先の名前が `micro:bit2_UART` と一致しているか |
| LED が ✕ になる | BLE が切断されている。Rover と Bridge の距離、電池の残量を確認する |
| 接続はするが走らない | 電池の残量、モータの配線を確認する。LED が矢印に変われば、走行指令は届いている |
| 前進から後退にすぐ切り替わらない | 仕様です。ギアを守るため、前進と後退の間には一度停止を挟む必要があります |

## ライセンス

本リポジトリのソースコードは [Apache License 2.0](LICENSE) で提供します。

ビルドに使う μT-Kernel 3.0（micro:bit 版）と benus は、本リポジトリには含まれていません。それぞれの提供元の条件に従ってください。詳しくは [NOTICE](NOTICE) を参照してください。

## 関連リンク

- benus（BLE NUS ドライバ）: https://github.com/koh523/benus
- micro:bit で μT-Kernel 3.0 を動かそう（PMC）: https://www.t-engine4u.com/info/mbit/2.html
