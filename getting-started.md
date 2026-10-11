## Getting Started with a2picoFN

### Overview

`a2picoFN` は、A2Pico2 シリーズを Apple II 実機の SmartPort カードとして動作させ、SmartPort 要求をホスト PC 上で動作する FujiNet-PC（APPLE ランタイム）へ中継するためのファームウェアプロジェクトです。

---

### Supported Hardware & Firmware Types

本プロジェクトでサポートするボードと、生成される UF2 ファイルの命名規則は以下の通りです：

* `a2picoFN_A2Pico2LiteW_<version>.uf2`: **A2Pico2Lite W** 専用（PICO_BOARD: `a2pico2litew`）
* `a2picoFN_A2Pico2-generic_<version>.uf2`: **A2Pico2Lite（非W）** を含む generic RP2350 A2Pico2 ボード用（PICO_BOARD: `a2pico2-generic`）

> **警告**: 対応していないファームウェアを書き込むと、カードや Apple II 本体に物理的損傷を与える恐れがあります。必ずお使いのハードウェアに合致するファームウェアを選択してください。

---

### Build Instructions

Pico SDK の仕様上、ボード定義（`PICO_BOARD`）ごとに生成される設定やキャッシュが異なるため、**ボードごとに独立したビルドディレクトリを使用してください**。未対応の `PICO_BOARD` を指定した場合、CMake configure 時にエラーとなります。

#### 1. A2Pico2Lite W 用ビルド (`build-litew`)

```bash
# Configure for A2Pico2Lite W
cmake -B build-litew -DPICO_BOARD=a2pico2litew

# Build
cmake --build build-litew
```

生成物: `build-litew/a2picoFN_A2Pico2LiteW_<version>.uf2`

#### 2. A2Pico2Lite（非W）用ビルド (`build-lite`)

```bash
# Configure for A2Pico2Lite (generic non-W)
cmake -B build-lite -DPICO_BOARD=a2pico2-generic

# Build
cmake --build build-lite
```

生成物: `build-lite/a2picoFN_A2Pico2-generic_<version>.uf2`

#### オプション: バージョン表記の指定

`VERSION` は CMake cache 変数として定義されており、既定値は日付（例: `2026-10-09`）です。任意の日付やバージョン文字列を指定する場合は、以下のように `-DVERSION=...` を渡します：

```bash
cmake -B build-litew -DPICO_BOARD=a2pico2litew -DVERSION=2026-10-09
```

---

### Flashing Firmware

標準の USB ケーブルを用いてファームウェアを書き込みます：

1. Apple II の電源を切り、A2Pico をスロットから取り外します。
2. A2Pico 上の `BOOTSEL` ボタンを押したままにします。
3. A2Pico を USB ケーブルで PC に接続します。
4. `BOOTSEL` ボタンを離します。PC 上に `RP2350`（または `RPI-RP2`）ドライブが認識されます。
5. 生成された `.uf2` ファイルをそのドライブにコピー（ドラッグ＆ドロップ）します。
6. コピー完了後、自動的に再起動します。USB ケーブルを外して完了です。

---

### Core0 Diagnostic Test Mode (FujiNet-PC 単体検証手順)

開発時・動作確認用に、FujiNet-PC（`SLIP_PROTOCOL=COM`）に対して実際にホストへ届く **INIT (Unit 1..N 走査)** → **STATUS (Unit 1)** → **READ_BLOCK (Unit 1 Block 0)** を順次自動送信し、応答を UART ログで確認できる診断テストモードを用意しています。

#### 1. 確認できた事実と制限事項 (Apple II 接続との関係)
* **確認できた事実**:
  * SLIP 通信レイヤ（`slip_command()`）の処理において、Apple II バス信号への同期待ちループは存在しないため、A2Pico を USB ケーブルで PC に接続した単体状態でも FujiNet-PC との USB CDC / SLIP 送受信自体は完了します。
  * `READ_BLOCK` 受信時に呼び出される `gen_read_block()` は、`firmware[]` の `OFFSET_BANK_2` ($2800) および `OFFSET_BANK_3` ($3000) に 6502 転送ルーチンコードを書き込みます。これは `6502/SSC.S` において `BANK 1` に配置された SmartPort ROM コード（`SMARTPORT.S`）とは異なる拡張バンク領域（`.RES $CF00-$C800`）を対象とする設計です。
* **制限事項・未検証点**:
  * `gen_read_page()` が生成するコードサイズがバンク予約領域（$C800〜$CEFF）や `firmware[]` 配列終端境界を侵害しないかの詳細なメモリ検証は未完了です。
  * Apple II 実機が未接続の状態では、生成された 6502 コードの実行および Apple II RAM へのブロックデータ展開サイクルは行われません。したがって、このテストは「FujiNet-PC との USB CDC / SLIP 通信の成立確認」に限定され、Apple II メモリへのデータ転送成立を保証するものではありません。

#### 2. テスト用ビルドの指定
テストモードを有効化するには、CMake configure 時に `-DTEST_MODE=ON` を指定します（デフォルトは `OFF`）：

```bash
# Configure with TEST_MODE enabled
cmake -B build-litew -DPICO_BOARD=a2pico2litew -DTEST_MODE=ON

# Build
cmake --build build-litew
```

生成された UF2 ファイルを通常手順（BOOTSEL）で A2Pico に書き込みます。

#### 3. FujiNet-PC 側の準備と起動
1. FujiNet-PC を `SLIP_PROTOCOL=COM` でビルドしておきます。
2. FujiNet-PC のドライブ 1 に有効な ProDOS ディスクイメージ（例: `Total Replay.hdv` や ProDOS 起動ディスク）をマウント設定しておきます。
3. A2Pico を USB ケーブルで PC に接続します。
4. FujiNet-PC を起動します（FujiNet-PC が A2Pico の USB CDC ポートを検出・オープンします）。

#### 4. 観察する UART ログ
USB CDC は SLIP 通信専用となるため、診断テストの進行ログはデバッグ UART（UART0, 115200 bps, 8N1）に出力されます。USB-シリアル変換アダプタを A2Pico の UART ピン（TX/RX）に接続してログを監視します。

> [!NOTE]
> **以下のログはあくまで例示（illustrative）です。**
> 検出されるユニット数、STATUS バイト（例: `0xF0`）、総ブロック数（例: `65536` blocks）、および Block 0 のデータ内容は、FujiNet-PC 側の設定やドライブ 1 にマウントされたディスクイメージ（容量やファイルシステム）に依存して変化するため、固定・保証された値ではありません。

**UART ログ出力例（参考例）**:
```
*** a2picoFN ***
SLIP Connect
  1 units

==================================================
=== [a2picoFN Core0 Diagnostic Test Mode] ===
==================================================
[TEST] Step 1 [Local Result]: INIT Scan Summary
       (Local Unit 0 loop scanned Unit 1..N against FujiNet-PC)
       Detected SmartPort units: 1
[TEST] Step 2 [Host Request]: Sending STATUS to Unit 1 (Device Status)
       Result: SUCCESS (retval = 0x00, length = 4 bytes)
       Device status byte: 0xF0 (Online: 1, WProt: 0)
       Total block count : 65536 blocks (32768 KB)
[TEST] Step 3 [Host Request]: Sending READ_BLOCK to Unit 1, Block 0
       Result: SUCCESS (retval = 0x00)
       Block 0 payload sample (first 16 bytes):
       XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX 
       (※マウントされたディスクイメージに応じた可変データ)
       [DMA Note] 6502 transfer routine written to firmware[] buffer.
                  Apple II RAM transfer execution remains untested.
==================================================
=== [Diagnostic Test Finished Successfully] ===
==================================================
```

#### 5. オンボード LED によるステータス確認（UART 配線なしの場合）
物理的なデバッグ UART（TX/RX ピン）への配線がない場合でも、A2Pico 基板上のオンボード LED の点灯・点滅パターンによってテスト結果を視覚的に判別できます：

| LED 状態 | 動作ステータス | 意味 |
| :--- | :--- | :--- |
| **消灯 (OFF)** | `TEST_STATUS_PENDING` | USB 接続待機中（FujiNet-PC 未起動・未接続）。 |
| **高速点滅 (100ms周期)** | `TEST_STATUS_RUNNING` | USB CDC SLIP 接続確立・テストシーケンス実行中。 |
| **常時点灯 (Solid ON)** | `TEST_STATUS_SUCCESS` | **全テスト成功**（INIT 走査、Unit 1 STATUS、Unit 1 Block 0 READ_BLOCK が全て正常応答）。 |
| **低速点滅 (500ms周期)** | `TEST_STATUS_FAILED` | **テスト失敗 / エラー**（0 units 検出、STATUS 失敗、READ_BLOCK 失敗、境界エラー）。 |

#### 6. 終了と通常版への戻し方
テストモードは 1 回実行完了すると停止します（LED 状態は継続表示されます）。通常ファームウェアに戻すには、`-DTEST_MODE=OFF`（または未指定）で再ビルドして書き込み直します：

```bash
# Reconfigure with TEST_MODE disabled (default)
cmake -B build-litew -DPICO_BOARD=a2pico2litew -DTEST_MODE=OFF

# Build
cmake --build build-litew
```

---

### Current Status & Notes

* **Phase 1 最小実装**: 現在の実装は、FujiNet-PC との USB CDC / SLIP 直接接続による SmartPort bridge です。
* **SSC なし / CDC 専用**: USB CDC を SLIP 専用とするため、SSC（シリアルカード）エミュレーション（`ser_task`）は起動せず、競合を排除しています。
* **FujiNet-PC 側の動作モード**: FujiNet-PC 側は `SLIP_PROTOCOL=COM` でビルド・実行し、macOS / PC の USB CDC ポートを直接オープンして対向します。
* **動作対象コマンド**: 今回の最小縦切り実装範囲は **INIT**、**STATUS**、**READ_BLOCK** です（WRITE_BLOCK、FORMAT、CONTROL 等は今後順次対応予定）。
* **Wi-Fi / TCP**: Wi-Fi / TCP 接続は今回実装しておらず、Phase 1 の有線 CDC 動作確認後の Phase 2 として計画されています。
