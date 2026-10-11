# a2picoFN 設計状況

最終更新: 2026-10-09

このファイルを、プロジェクトで合意した設計方針、調査結果、確定事項、未解決事項の一覧として使う。

---

## プロジェクト概要

- リポジトリ名: `a2picoFN`
- 説明: **Apple II SmartPort bridge to FujiNet-PC**
- 目的: A2Pico2（RP2350）を Apple II の SmartPort カードとして動作させ、SmartPort 要求をホスト PC 上の FujiNet-PC（APPLE ランタイム）へ中継する。
- 参照リソース:
  - FujiNet 版 AppleWin ZIP: `refs/AppleWin-master（fujinet）.zip`
  - FujiNet ファームウェア一次ソース: `~/dev/fujinet-firmware`
  - A2Pico ハードウェア / ボード一次ソース: `~/dev/a2pico` および `FetchContent (oliverschmidt/a2pico)`

---

## 確定事項

### 1. 対象ボードとビルド構成（CMake）

- **サポート対象ボード**:
  - `A2Pico2Lite W`: `PICO_BOARD=a2pico2litew`
  - `A2Pico2Lite（非W）`: `PICO_BOARD=a2pico2-generic`
  - 上記以外の `PICO_BOARD` または未指定は CMake configure 時に `FATAL_ERROR` で停止する（[CMakeLists.txt:L6-L12](file:///Users/oshuno/dev/a2picoFN/CMakeLists.txt#L6-L12)）。
- **FetchContent 方式の維持**:
  - `oliverschmidt/a2pico` を FetchContent で取得（[CMakeLists.txt:L14-L19](file:///Users/oshuno/dev/a2picoFN/CMakeLists.txt#L14-L19)）。
  - `a2pico` の `CMakeLists.txt:L1` にて `set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards PARENT_SCOPE)` が設定されるため、呼び出し元での重複設定は行わない。
- **ターゲットおよび UF2 命名規則**:
  - 形式: `a2picoFN_<board suffix>_<version>.uf2`（例: `a2picoFN_A2Pico2LiteW_2026-10-09.uf2`, `a2picoFN_A2Pico2-generic_2026-10-09.uf2`）
  - `VERSION` は CMake cache 変数（日付既定値: `2026-10-09`）とする（[CMakeLists.txt:L25-L26](file:///Users/oshuno/dev/a2picoFN/CMakeLists.txt#L25-L26)）。
- **独立ビルドディレクトリの運用**:
  - ボードごとのキャッシュ競合を防ぐため、`build-litew` と `build-lite` に分離（[getting-started.md:L23-L47](file:///Users/oshuno/dev/a2picoFN/getting-started.md#L23-L47)）。
- **ローカル固有パスの非ハードコード**:
  - `PICO_SDK_PATH` 等の環境依存パスを CMakeLists.txt にハードコードしない。

### 2. 通信 Transport 設計方針（アーキテクチャ目標と現在地）

- **プロジェクトの主目標（Primary Target Architecture）**:
  - **A2Pico2Lite W における Wi-Fi / TCP（A2Pico 側が Port 1985 で Listen）による FujiNet-PC との完全無線通信**が本プロジェクトの本命・最終ターゲットアーキテクチャです。
  - FujiNet-PC（`SLIP_PROTOCOL=NET`）は TCP クライアントとして動作するため、A2Pico 側が lwIP / CYW43 により TCP ポート 1985 で待受（Listen）を行い、FujiNet-PC からの接続を受け付けます。
- **USB CDC の位置付け（Provisional Stepping Stone）**:
  - USB CDC（`SLIP_PROTOCOL=COM`）は、Apple II SmartPort プロトコル変換および JIT ブロック転送の垂直統合（最小縦切り）を迅速に実証するための**暫定的な足がかり（Provisional Stepping Stone）**です。
- **トランスポート抽象化レイヤ（Transport Abstraction Layer）**:
  - 将来の Wi-Fi / TCP 実装時に SLIP フレーミングや SmartPort ディスパッチの大規模改修を防ぐため、通信媒体を `transport.h`（`transport_init`, `transport_task`, `transport_is_connected`, `transport_read_byte`, `transport_write_byte`, `transport_flush`）として抽象化しました。
  - SLIP プロトコル層（`slip.c`）は物理層（USB CDC か TCP ソケットか）に一切依存せず、共通のバイトストリーム API のみを扱います。
  - **実装構成**:
    - `transport_cdc.c` (Phase 1 暫定): TinyUSB Device CDC による実装。
    - `transport_wifi.c` (Phase 2 本命): CYW43 + lwIP TCP Server（Port 1985）による実装（将来追加）。
- **ボードと通信方式の対応マトリクス（現状と計画）**:

  | ボード (`PICO_BOARD`) | 現状の実装 | 将来計画: USB CDC | 将来計画: Wi-Fi (TCP) |
  | --- | :---: | :---: | :---: |
  | **A2Pico2Lite（非W）** (`a2pico2-generic`) | USB CDC ビルド | 対応 | **非対応（configure 時エラー）** |
  | **A2Pico2Lite W** (`a2pico2litew`) | USB CDC ビルド (暫定) | 対応 | **本命対応（Port 1985 Listen）** |

- **フェーズ計画**:
  - **Phase 1（暫定版・最小実装完了）**: USB CDC 上での SLIP 通信。トランスポート抽象化レイヤ（`transport.h` + `transport_cdc.c`）を導入し、両ボードで共通して動作可能な最小縦切り実装（INIT / STATUS / READ_BLOCK）。
  - **Phase 2（本命版・CYW43 Wi-Fi）**: A2Pico2Lite W における CYW43 Wi-Fi 経由の TCP 1985 接続（`transport_wifi.c` 実装）。

### 3. Phase 1 CDC 最小実装（実装内容）

- **USB CDC の SLIP 専用化と SSC 競合排除**:
  - `main.c` の `io_task()` から `ser_task();` の呼び出しを排除（[main.c:L41-L47](file:///Users/oshuno/dev/a2picoFN/main.c#L41-L47)）。USB CDC (ITF 0) の受信文字横取りや送信フレーム破壊を解消。
  - `board.c` の `serial_true_put()` を無効化（[board.c:L238-L244](file:///Users/oshuno/dev/a2picoFN/board.c#L238-L244)）。Apple II 側からの SSC モード切替信号を無視し、常時 SmartPort モードを維持。
- **純粋 SmartPort Bridge 動作への直結**:
  - `sp.c` の `sp_init()` でのローカルストレージ初期化（`hdd_init()`）をバイパス（[sp.c:L118-L120](file:///Users/oshuno/dev/a2picoFN/sp.c#L118-L120)）。
  - `sp.c` の `sp_task()` でローカル SD チェック（`hdd_mounted()`）を行わず、常時 `slip_task()` および `slip_command()` を直接駆動（[sp.c:L185-L208](file:///Users/oshuno/dev/a2picoFN/sp.c#L185-L208)）。
- **既存 SLIP 処理の活用とスコープ限定**:
  - A2retroNET の `slip.c` に既に成立していたプロトコル処理を活用。
  - **Unit 0 要求とホスト要求の仕様（確認された事実）**:
    - `Unit 0 STATUS` ([slip.c:L299-L311](file:///Users/oshuno/dev/a2picoFN/slip.c#L299-L311)): SmartPort コントローラステータス要求であり、FujiNet-PC へは送信されず Pico ローカルで合成して即座に応答を返す。
    - `Unit 0 INIT` ([slip.c:L312-L328](file:///Users/oshuno/dev/a2picoFN/slip.c#L312-L328)): Pico ローカルの走査ループであり、実際に FujiNet-PC へ送信される要求は Unit 1..N への INIT コマンドである。
    - ホスト宛てに実際の SLIP パケットが送信されるのは `Unit > 0`（Unit 1..N の STATUS, READ_BLOCK 等）である。
  - **WRITE_BLOCK**、**FORMAT**、**CONTROL** 等は今回の最小縦切りスコープ外とし、既存コードは削除せず保持。
  - ホスト対向モードは FujiNet-PC の `SLIP_PROTOCOL=COM`（直接シリアルオープン）に限定。Wi-Fi / TCP は未実装。
- **開発用 Core0 診断テストモード（TEST_MODE）の実装**:
  - `-DTEST_MODE=ON`（デフォルト OFF）で有効化される独立モジュール（[test.c](file:///Users/oshuno/dev/a2picoFN/test.c), [test.h](file:///Users/oshuno/dev/a2picoFN/test.h)）を実装。通常 UF2 には一切影響を与えない。
  - 実際にホスト（FujiNet-PC）へ届く要求をテスト対象とし、ローカル結果と明確に区別：
    - Step 1 [Local Result]: INIT スキャン結果（Unit 0 INIT によるローカル走査ループで、FujiNet-PC に対する Unit 1..N 走査が完了した結果の `units` 表示）。
    - Step 2 [Host Request]: Unit 1 に対する STATUS 要求（FujiNet-PC へ実際に SLIP パケット送信し、デバイスステータスを受信）。受信データ長境界チェック（`sts_len >= 4`）を実施。
    - Step 3 [Host Request]: Unit 1, Block 0 に対する READ_BLOCK 要求（FujiNet-PC へ実際に SLIP パケット送信し、512 バイトブロックを受信）。
  - 要求バッファは各ステップ実行前に `memset` で確実に初期化。
  - `gen_read_block()` のメモリ安全性・境界検証結果（静的解析完了）:
    - **事実（構造とメモリ配置）**: SLIP 通信レイヤには Apple II バス待機ループが存在せずパケット送受信は単体で完了する。`gen_read_block()` の書き込み先は `firmware[]` の `OFFSET_BANK_2` ($2800) / `OFFSET_BANK_3` ($3000) であり、`6502/SSC.S` 上で `BANK 1` に配置された SmartPort ROM コード（$2000〜$27FF）とは物理的に完全分離された拡張バンク予約領域（`.RES $CF00-$C800` = 1,792 バイト）を対象とする。
    - **生成コード長の定量評価（JIT 境界検証）**:
      - 最小コード長: **771 バイト**（全バイト同一値: 初期化ロード 2 バイト + 256×3 バイト store + RTS 1 バイト）。
      - 最大コード長: **1,281 バイト**（全バイトが不連続かつ非0/非$80の最悪ケース: 256×2 バイト load + 256×3 バイト store + RTS 1 バイト）。
      - 境界マージン: 予約領域（1,792 バイト）に対し最悪ケース（1,281 バイト）でも **511 バイトの安全マージン** が確保され、バンク末尾の `SSC.CF00.S`（$CF00〜$CFFF）やバンク境界（2,048 バイト境界、マージン 767 バイト）を侵食することは絶対にない。BANK 1 の SmartPort ROM も 100% 保持される。
    - **制限・残る実機確認事項**: Apple II 未接続状態（Core0 単体テスト）では 6502 実行および実機 RAM へのデータ展開サイクル自体は未実行。実機 Apple II 接続時に 6502 CPU がこの JIT コードを実行して RAM へのデータ展開が正しく完了することの確認が Phase 1-3 の実機検証課題となる。
- **Core 間同期と初期化ハーデニング（実機接続準備）**:
  - `sp_init()` において起動直後に明示的に `sp_reset()` を呼び出し、`sp_control`、`sp_read_offset`、`sp_write_offset` を完全初期化。
  - ARM Cortex-M33（RP2350）における Core 間共有メモリの可視性を保証するため、`sp.c:sp_task()` の `sp_control = CONTROL_DONE` 直前、および `board.c:sp_control_put()` の `sp_control = data` 直前に `__dmb()`（Data Memory Barrier）を導入。Core 1 と 6502 データバスへのペイロード反映順序を厳格に保護。
- **Phase 4（Phase 1-4）: Apple II 実機ブートハング（`BOOT1` ループ）の解消**:
  - `PR#7`（$C700）実行時に 6502 側 `BOOT1`（`6502/SMARTPORT.S:L354-L365`）が発行する `CONTROL_CONFIG`（$40）に対し、Pico Core0（`sp.c:sp_task`）が何も戻り値を設定せず `sp_buffer[0] == 0` のまま完了させていたため、6502 側が `BNE DOBOOT` を通過できず無限 `RETRY` ループに陥っていた事象を特定。
  - `BOOT1` loop unblocked by setting `sp_buffer[0] = 1` (`DELAY_DONE`) on `CONTROL_CONFIG` in `sp.c`: `sp_buffer[0] = 1;`（`DELAY_DONE`）を設定することにより、6502 が直ちに `DOBOOT` へ進み ProDOS Block 0 読み込みへ移行できるように修正。
- **Phase 4.1: ProDOS / SmartPort WRITE ブロック書き込み（`SAVE`）ハング解消**:
  - **事象と原因**: `SAVE DUM` 実行時に Apple II がハングし、FujiNet-PC が WRITE コマンドを受信しない不具合。6502 側は `6502/SMARTPORT.S:WRBLOCK` により 512 バイトを正常に `$CFF0` へ書き込み転送していたが、`slip.c:send_request()` が 522 バイト超のパケットを一括送信する際に `tusb_config.h` の `CFG_TUD_CDC_TX_BUFSIZE`（64 バイト）を超過し、`transport_task()` ポンプ不足により 64 バイト以降および終端 `END`（`0xC0`）がドロップされていた。
  - **CDC TX FIFO 拡張とフロー制御**: `tusb_config.h` の `CFG_TUD_CDC_TX_BUFSIZE` を 1024 バイトへ拡張。さらに `transport_cdc.c:transport_write_byte()` にフロー制御（FIFO 空き待ちループ `tud_cdc_write_flush()` + `tud_task()`）を導入し、パケット欠落を完全に防止。
  - **ヘッダパラメータ初期化**: `slip.c:convert_protocol()` の `case SP_CMD_WRITEBLK:` において、未初期化だった `buffer[SP_I_BLK_BUFF + 0]` / `buffer[SP_I_BLK_BUFF + 1]` を `sp_buff_lo` / `sp_buff_hi` で正常に初期化。

### 4. ハードウェア・ボード差異（一次ソース確認結果）

- **A2Pico2Lite W (`a2pico2litew`)**:
  - ソース: `a2pico-src/boards/a2pico2litew.h`
  - MCU: RP2350A（30ピンGPIO）、Flash 4MB。
  - Wi-Fi: CYW43439 搭載（`PICO_CYW43_SUPPORTED 1`）。
  - PIO: `HAVE_ENBL` 有効。`GPIO_ENBL` (26), `GPIO_RESET` (27) を使用。PIO0 のみで 4 SM（addr, read, write, sync）を消費。PIO1/PIO2 は空き。
- **A2Pico2Lite（非W generic: `a2pico2-generic`)**:
  - ソース: `a2pico-src/boards/a2pico2-generic.h`
  - MCU: RP2350B / RP2350A generic、Flash 2MB。
  - Wi-Fi: なし。
  - PIO: `HAVE_ENBL` なし。個別信号線 `GPIO_DEVSEL` (26), `GPIO_IOSEL` (27), `GPIO_IOSTRB` (28) を使用。PIO0 で 4 SM、PIO1 で 3 SM（devsel, iosel, iostrb）の計 7 SM を消費。

### 5. A2retroNET 内部構造と Core 間同期

- **Core1 bus 処理 ([board.c:L257-L293](file:///Users/oshuno/dev/a2picoFN/board.c#L257-L293))**:
  - Apple II バスを 200MHz でタイトにポーリング。
  - `$CFF0`（SmartPort データバッファ: [board.c:L162-L168, L188-L193](file:///Users/oshuno/dev/a2picoFN/board.c#L162-L168)）。
  - `$CFF1`（SmartPort コントロールレジスタ: [board.c:L170-L175, L195-L199](file:///Users/oshuno/dev/a2picoFN/board.c#L170-L175)）。
- **Core0 `sp_task` ([sp.c:L185-L305](file:///Users/oshuno/dev/a2picoFN/sp.c#L185-L305))**:
  - 共有バッファ `sp_buffer[0x400]`（1024バイト）およびハンドシェイク変数 `sp_control`（[sp.h:L36-L39](file:///Users/oshuno/dev/a2picoFN/sp.h#L36-L39)）。
  - 6502 側は `$CFF1` にコマンドを書き込んだ後、ビット 7（`CONTROL_DONE` = 0x80）が立つまでポーリング待機する。
  - **評価**: Core1 のバスループ自体はノンブロッキングで回転し続けるため、Core0 がホスト通信待ちで停止しても Core1 のリアルタイム応答性は阻害されない。
- **`ser_task` との CDC 競合 ([main.c:L41-L48](file:///Users/oshuno/dev/a2picoFN/main.c#L41-L48), [ser.c:L32-L53](file:///Users/oshuno/dev/a2picoFN/ser.c#L32-L53))**:
  - `ser_task` が TinyUSB CDC (ITF 0) を直接読み書きして SIO FIFO とやり取りしている。
  - SLIP transport が同じ CDC ITF 0 を使用するとフレーム破壊・競合が発生する。Phase 1 では `ser_task` を呼び出さない構成として競合を解消した。

### 6. FujiNet-PC 側の接続仕様（一次ソース確認結果）

- **FujiNet-PC の役割**:
  - ソース: `fujinet-firmware/lib/bus/iwm/iwm_slip.cpp:L121-L180`
  - FujiNet-PC は **SmartPort スレーブ（デバイス側）** として動作し、リクエストを受信してレスポンスを返す。
  - したがって、A2Pico（`slip.c`）はマスター（ホスト要求発行側）として FujiNet-PC に対向する。
- **`SLIP_PROTOCOL=COM` ([connector_com.cpp:L25-L65](file:///Users/oshuno/dev/fujinet-firmware/lib/bus/iwm/connector_com.cpp#L25-L65))**:
  - `libserialport` を使用。`SP_TRANSPORT_USB` に一致する最初に見つかった USB CDC ポートを自動オープンする。
  - ポート名指定オプションが存在しないため、macOS に複数の USB シリアル機器が存在する場合に誤認識のリスクがある（要検証）。
- **`SLIP_PROTOCOL=NET` ([connector_net.cpp:L55-L100](file:///Users/oshuno/dev/fujinet-firmware/lib/bus/iwm/connector_net.cpp#L55-L100))**:
  - FujiNet-PC は **TCP クライアント** として動作し、設定されたホスト/IP（ポート 1985）へ `connect()` する。
  - 対向側（AppleWin または A2Pico Wi-Fi、または PC 上の TCP-CDC ブリッジ）が **TCP サーバ（Listen 側）** になる必要がある。

---

## 未解決事項・要検証一覧

### 1. ホスト接続・Transport
- [ ] **macOS 上での FujiNet-PC `SLIP_PROTOCOL=COM` の安定性検証**:
  - 複数 USB シリアル接続時のポート誤認識対策（コマンドラインまたは設定ファイルでのポート名明示指定パッチが必要か）。
  - 切断・再接続時の `connector_com::create_connection()` リトライ挙動の実機確認。
- [ ] **Wi-Fi Transport（Phase 2: 設計提案と残る検討課題）**:
  - **確定仕様（一次ソース）**: FujiNet-PC の NET（[connector_net.cpp:L55-L100](file:///Users/oshuno/dev/fujinet-firmware/lib/bus/iwm/connector_net.cpp#L55-L100)）は TCP クライアントとして動作し、相手側が listener であることが確定。
  - **採用方針案**: A2Pico2Lite W 側で lwIP / CYW43 を用いて TCP ポート 1985 の listener を実装する方針とする。
  - **残る実装上の検討事項**:
    - Wi-Fi SSID / パスワードの設定・保持方式（設定ファイルまたはオンボード UI）。
    - ネットワーク切断・Wi-Fi 切断時の再接続（reconnect）シーケンスとタイムアウト処理。
    - DHCP / 静的 IP の割り当てと、FujiNet-PC 側（`fnconfig.ini`）への IP アドレス設定・認識手順。

### 2. SmartPort / SLIP プロトコル整合性
- [x] **CONTROL コマンドのペイロード長ヘッダ不一致の解消（検証完了・対応済）**:
  - FujiNet-PC / AppleWin `Request.cpp:L48`（`packet.begin() + 11 + 2` で長さ 2 バイトをスキップ）の仕様に基づき、`slip.c` の `send_request()` において `skip = 0` および `size += ... + 2;` に変更。2 バイトの長さヘッダ（`SP_I_CTRL_CNT`）を含めて送信することで、長さ 0 の CONTROL コマンド（`MOUNT_HOST` 等）における `packet.size() < 13` による `std::length_error` クラッシュを解消。
- [ ] **FujiNet-PC 起動時の INIT スキャンと Apple II ブートシーケンスの同期**:
  - FujiNet-PC が接続時に Unit 1 からスキャンして認識する手順と、Apple II 起動時の ProDOS / SmartPort INIT スキャンが競合・不整合を起こさないシーケンス設計。

### 3. ファームウェア内部設計と実機検証課題
- [x] **CDC インターフェースの整理**:
  - Phase 1 暫定版として SSC を完全に無効化し、Single CDC を SLIP 専用とする構成で実装完了。
- [x] **Core0 診断テストモードによる単体疎通テスト（ホスト要求の確認）**:
  - `-DTEST_MODE=ON` でビルドしたファームウェアを A2Pico に書き込み、FujiNet-PC（`SLIP_PROTOCOL=COM`）と USB 接続して実機検証完了。INIT スキャン（Unit 1..9 検出）→ STATUS (Unit 1, 6バイト) → READ_BLOCK (Unit 1 Block 0, 514バイト) および CYW43 オンボード LED の点灯・点滅制御の正常動作を確認。
- [x] **`gen_read_block()` の `firmware[]` メモリ安全性と境界検証**:
  - `gen_read_page()` が生成する 6502 コード長（771〜1,281 バイト）が BANK 2 / BANK 3 の予約領域（各 1,792 バイト）内に完全に収まり、511 バイト以上の安全マージンが確保され配列終端や ROM 領域を侵害しないことを静的解析により確認完了。
- [ ] **Apple II 実機接続テスト（Phase 1 最小縦切りの確認）**:
  - 通常版ファームウェア（`-DTEST_MODE=OFF`）を A2Pico に書き込み、Apple II スロットに装着した状態で FujiNet-PC（`SLIP_PROTOCOL=COM`）と接続。Apple II 電源投入時に INIT スキャン → STATUS → READ_BLOCK（ブロック 0 読み取り・ProDOS ブート開始）および 6502 による実機 RAM へのデータ転送が正常に成立するか実機検証する。
- [ ] **タイムアウト値の調整**:
  - ProDOS / 6502 側のポーリング上限時間と、Core0 / SLIP 側のタイムアウト時間（現在 `slip.c` では 100ms〜5000ms）のバランス評価。
