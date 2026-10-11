# a2picoFN: SmartPort bridge to FujiNet-PC 詳細設計メモ

作成日: 2026-10-09  
対象リポジトリ: `~/dev/a2picoFN`  
参照一次ソース:
- A2retroNET / a2picoFN 現行ソース: `a2picoFN/board.c`, `sp.c`, `slip.c`, `ser.c`, `main.c`, `CMakeLists.txt`
- AppleWin FujiNet 版ソース: `a2picoFN/refs/AppleWin-master（fujinet）.zip`
- FujiNet-PC (fujinet-firmware) 一次ソース: `~/dev/fujinet-firmware`
- A2Pico ハードウェア / ボード一次ソース: `~/dev/a2pico`

---

## 1. A2retroNET 内部アーキテクチャ調査

### 1.1 Core1 bus 処理 (`board.c`)
- **エントリーポイント**: `board()` ([board.c:L257-L293](file:///Users/oshuno/dev/a2picoFN/board.c#L257-L293))
  - Core1 で起動直後に `busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;` ([main.c:L51](file:///Users/oshuno/dev/a2picoFN/main.c#L51)) によりバスクロックを優先獲得し、200MHz で Apple II バス信号をタイトにポーリングします。
  - `a2pico_getaddr()` でアドレス・ストローブ信号をラッチし、アドレスデコードを行います。
- **アドレス空間デコード**:
  - `$Cn00`〜`$CnFF` (`io && !strb`):
    - カード ROM の読み出し: `a2pico_putdata(firmware[offset + addr])` ([board.c:L276](file:///Users/oshuno/dev/a2picoFN/board.c#L276))
    - カード選択検出時に `active = true; self = (addr & 0x0700) | 0x0007; firmware[self] = 0x00;` ([board.c:L287-L291](file:///Users/oshuno/dev/a2picoFN/board.c#L287-L291)) により SmartPort として自身を識別。
  - `$CFF0`〜`$CFFF` (`addr >= 0x0FF0`):
    - `$CFF0` (データレジスタ):
      - Read: `sp_data_get()` → `a2pico_putdata(sp_buffer[sp_read_offset++])` ([board.c:L162-L168](file:///Users/oshuno/dev/a2picoFN/board.c#L162-L168))
      - Write: `sp_data_put()` → `sp_buffer[sp_write_offset++] = data` ([board.c:L188-L193](file:///Users/oshuno/dev/a2picoFN/board.c#L188-L193))
    - `$CFF1` (コントロールレジスタ):
      - Read: `sp_control_get()` → `a2pico_putdata(sp_control)` ([board.c:L170-L175](file:///Users/oshuno/dev/a2picoFN/board.c#L170-L175))
      - Write: `sp_control_put()` → `sp_control = data` ([board.c:L195-L199](file:///Users/oshuno/dev/a2picoFN/board.c#L195-L199))
    - `$CFFF` (非活性化レジスタ):
      - Read/Write: `active = false;` ([board.c:L177-L179, L246-L248](file:///Users/oshuno/dev/a2picoFN/board.c#L177-L179))
  - `$C080`〜`$C0FF` (`!io`):
    - SSC (ACIA 6551) エミュレーションレジスタ群。データは RP2040/RP2350 の SIO FIFO (`sio_hw->fifo_rd` / `sio_hw->fifo_wr`) へ直接受け渡されます。

### 1.2 Core0 `sp_task` と Core 間ハンドシェイク (`sp.c`)
- **共有バッファと制御変数**:
  - `sp_buffer[0x0400]` (1024バイト): コマンドパラメータ、読み書きブロックデータ用 ([sp.h:L37](file:///Users/oshuno/dev/a2picoFN/sp.h#L37))
  - `sp_control` (ハンドシェイク用 8bit 変数):
    - `CONTROL_NONE (0x00)`, `CONTROL_PRODOS (0x01)`, `CONTROL_SP (0x02)`, `CONTROL_CONFIG (0x40)`, `CONTROL_DONE (0x80)` ([sp.h:L30-L35](file:///Users/oshuno/dev/a2picoFN/sp.h#L30-L35))
- **同期シーケンス**:
  1. 6502 (Apple II) 側ドライバが `$CFF0` 経由で `sp_buffer` にコマンドパケット（コマンド、Unit 番号、バッファポインタ等）を書き込みます。
  2. 6502 が `$CFF1` に `CONTROL_SP (0x02)` または `CONTROL_PRODOS (0x01)` を書き込みます。
  3. Core0 の `sp_task()` ([sp.c:L185-L305](file:///Users/oshuno/dev/a2picoFN/sp.c#L185-L305)) が `sp_control` の変化を検知し、`slip_command()` を実行します。
  4. 6502 側は `$CFF1` のビット 7 (`CONTROL_DONE = 0x80`) が 1 になるまでポーリングループで待機します。
  5. Core0 が処理完了（ホスト応答受信完了またはタイムアウト）すると、`sp_read_offset = sp_write_offset = 0; sp_control = CONTROL_DONE;` をセットします ([sp.c:L206-L207](file:///Users/oshuno/dev/a2picoFN/sp.c#L206-L207))。
  6. 6502 側が `$CFF0` からリターンコードおよび読み取りデータを取得して処理を完了します。
- **FIFO の利用実態**:
  - RP2350 の Hardware SIO FIFO (`multicore_fifo_*`) は **SSC（シリアル通信）専用** に使われており、SmartPort 通信には一切関与していません。SmartPort は純粋に `sp_buffer` と `sp_control` の共有メモリ方式で同期されています。

### 1.3 USB CDC の SLIP 送受信と INIT スキャン (`slip.c`)
- **SLIP フレーミング**:
  - エスケープ規則: `END = 0xC0`, `ESC = 0xDB`, `ESC_END = 0xDC`, `ESC_ESC = 0xDD` ([slip.c:L35-L38](file:///Users/oshuno/dev/a2picoFN/slip.c#L35-L38))。
  - 送信形式: `[END] [sequence_number] [buffer 10バイト] [可変長ペイロード] [END]` ([slip.c:L122-L157](file:///Users/oshuno/dev/a2picoFN/slip.c#L122-L157))。
- **接続時 INIT スキャンの流れ**:
  - `tud_cdc_connected()` が成立すると、`slip_task()` はホストに対して Unit 0 の INIT を開始します ([slip.c:L378-L398](file:///Users/oshuno/dev/a2picoFN/slip.c#L378-L398))。
  - Unit 0 の INIT 処理 ([slip.c:L342-L358](file:///Users/oshuno/dev/a2picoFN/slip.c#L342-L358)):
    - `units = 0` から開始し、Unit 1, 2, ... と順次ホストへ `SP_CMD_INIT` を送信。
    - ホストが `SP_SUCCESS` を返さなくなるまでループし、接続されたデバイス総数 `units` を確定。
    - 後続の Apple II からの Unit 0 に対する `SP_CMD_STATUS` 要求に対して、この `units` 数をコントローラステータスとして応答。

### 1.4 `ser_task` が同じ CDC を読み書きする影響
- `main.c` の `io_task()` 内で `ser_task()` が呼ばれています ([main.c:L46](file:///Users/oshuno/dev/a2picoFN/main.c#L46))。
- `ser_task()` は `tud_cdc_read_char()` / `tud_cdc_write_char()` を使用して SIO FIFO とやり取りします ([ser.c:L38, L50](file:///Users/oshuno/dev/a2picoFN/ser.c#L38))。
- **重大な影響**:
  - `slip.c` の `recv_byte()` / `send_byte()` も同一の TinyUSB CDC インターフェース 0 を使用しています。
  - `ser_task()` が有効なままだと、ホストから受信した SLIP バイトを `ser_task` が奪い取って破棄したり、SSC 送信データを SLIP ストリームに混入させてフレーム破壊を引き起こします。
  - **対策**: SmartPort bridge 動作時は `ser_task` の呼び出しを完全に無効化するか、Dual CDC 構成（Interface 0: SLIP, Interface 1: SSC）に分離する必要があります。

---

## 2. FujiNet-PC APPLE ランタイム接続仕様

### 2.1 `SLIP_PROTOCOL=COM`（macOS 上での動作）
- **実装ファイル**: `fujinet-firmware/lib/bus/iwm/connector_com.cpp` ([connector_com.cpp:L25-L65](file:///Users/oshuno/dev/fujinet-firmware/lib/bus/iwm/connector_com.cpp#L25-L65))
- **ポート列挙とオープン**:
  - `libserialport` の `sp_list_ports()` を使用し、`sp_get_port_transport(port) == SP_TRANSPORT_USB` となる最初に見つかったポートを自動オープンします。
  - `sp_set_dtr(port, SP_DTR_ON)` を設定して接続を確立。
- **接続方向と役割**:
  - **FujiNet-PC 側がシリアルポートをオープン（接続開始）します**。A2Pico が USB 接続され CDC デバイスとして認識されていれば通信が成立します。
  - FujiNet-PC (`iwm_slip.cpp`) は **SmartPort デバイス側（スレーブ）** として動作し、リクエストを待機して応答します。
  - A2retroNET の `slip.c` と SLIP フレーミング（`0xC0`, `0xDB`）は完全に合致しています。
- **潜在的課題**:
  - macOS 上で他に USB シリアルデバイス（Arduino や デバッグ UART アダプタ等）が接続されている場合、ポート名固定オプションが無いため誤ったデバイスを開いてしまうリスクがあります。

### 2.2 `SLIP_PROTOCOL=NET`（TCP 1985）との違い
- **実装ファイル**: `fujinet-firmware/lib/bus/iwm/connector_net.cpp` ([connector_net.cpp:L55-L100](file:///Users/oshuno/dev/fujinet-firmware/lib/bus/iwm/connector_net.cpp#L55-L100))
- **接続方向**:
  - **FujiNet-PC は TCP クライアントとして動作します**（設定された IP/ホストの Port 1985 へ `connect()` を試みる）。
  - 対向側（AppleWin または A2Pico）は **TCP サーバ（Listen 側）** である必要があります。
- **本命ターゲット（A2Pico2Lite W Wi-Fi）との整合**:
  - A2Pico2Lite W では、CYW43 および lwIP を用いて A2Pico 自身が TCP ポート 1985 の Listener（サーバ）として動作します。
  - FujiNet-PC からの接続を直接受け入れるため、PC 側の中継スクリプト等を介さずに直接無線通信が成立します。
- **TCP 中継が必要になる条件（暫定 CDC 時の代替案）**:
  - FujiNet-PC が `SLIP_PROTOCOL=NET`（TCP クライアント）でビルドされており、かつ A2Pico が USB CDC（シリアル）で接続されている場合。
  - この場合はホスト PC 上で「TCP ポート 1985 で Listen し、FujiNet-PC からの接続を受け、USB CDC との間で SLIP パケットを双方向中継するブリッジプログラム（Python スクリプトや socat）」が必要となります。
  - 一方、FujiNet-PC を `SLIP_PROTOCOL=COM` でビルドすれば USB CDC を直接開けるため、Phase 1 の検証はこの方式を採用しています。

### 2.3 トランスポート抽象化アーキテクチャ (Transport Decoupling)

- **設計思想と本命目標**:
  - **Wi-Fi / TCP（A2Pico 側が Port 1985 で Listen）が a2picoFN の本命・主要ターゲットアーキテクチャ**であり、USB CDC は Phase 1 SmartPort プロトコル検証のための**暫定的な足がかり（Provisional Stepping Stone）**です。
  - 将来 CYW43 / lwIP を導入する際に SLIP フレーミングや SmartPort ディスパッチの大規模改修が発生するのを防ぐため、トランスポート層を `transport.h` に抽象化して物理媒体から分離しました。
- **責務分離の原則**:
  - `board.c` (Core 1): 200MHz Apple II バス同期・レジスタ I/O（`$CFF0`, `$CFF1`）。
  - `sp.c` (Core 0): SmartPort / ProDOS コマンド同期・共有メモリハンドシェイク。
  - `slip.c` (Core 0): SLIP エスケープ（`END`, `ESC`）および SmartPort 要求・応答のパケットフレーミング。物理通信媒体に一切依存しない。
  - `transport.h` / `transport_*.c`: 物理・論理通信層の隠蔽。
    - `transport_cdc.c` (Phase 1 暫定): TinyUSB Device CDC によるバイトストリーム I/O。
    - `transport_wifi.c` (Phase 2 本命): CYW43 + lwIP TCP Server（Port 1985 Listen）によるバイトストリーム I/O（将来追加）。

### 2.4 WRITE_BLOCK / WRITE 送信における CDC TX バッファ管理とフロー制御
- **問題の本質**:
  - `WRITE_BLOCK` 要求は 11 バイトのヘッダに加えて 512 バイトのデータペイロード（および SLIP エスケープ文字群）を送信します（計 523 バイト以上）。
  - `tusb_config.h` の初期設定では `CFG_TUD_CDC_TX_BUFSIZE` が 64 バイトしかなく、`send_request()` 中に `transport_task()` (`tud_task()`) の呼び出しやフラッシュが行われない場合、64 バイト以降のデータおよび終端 `END`（`0xC0`）が無音破棄され、FujiNet-PC 側でフレーム不完全となり WRITE 要求が処理されず Apple II がハングアップする原因となっていました。
- **恒久対策**:
  1. **CDC TX FIFO 拡張**: `tusb_config.h` の `CFG_TUD_CDC_TX_BUFSIZE` を 1024 バイトへ拡張。
  2. **トランスポート層フロー制御**: `transport_cdc.c:transport_write_byte()` において、`tud_cdc_write_char()` が 0（FIFO フル）を返した場合に `tud_cdc_write_flush()` と `tud_task()` を回してバッファ空きを待機するループ（切断時は安全に break）を導入。
  3. **プロトコル変換ヘッダ整合性**: `slip.c:convert_protocol()` の `case SP_CMD_WRITEBLK:` において、`buffer[SP_I_BLK_BUFF + 0]` / `buffer[SP_I_BLK_BUFF + 1]` に `sp_buff_lo` / `sp_buff_hi` を明示的にセットし、SmartPort 要求パケットヘッダの整合性を確保。

---

## 3. SmartPort / devrelay 要求・応答プロトコル対応表

AppleWin devrelay（`AppleWin-master/source/devrelay/`）および FujiNet `iwm_slip` と A2retroNET のコマンド送受信仕様の対比表：

| コマンド名 | Cmd ID | リクエスト ヘッダ長 | リクエスト 追加ペイロード | レスポンス ヘッダ長 | レスポンス 追加データ | 備考・ステータス長 |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **INIT** | `0x05` | 11 B | なし (0 B) | 2 B | なし (0 B) | `[seq, status]`. デバイス初期化・存在確認 |
| **STATUS** | `0x00` | 11 B | なし (0 B) | 2 B | 可変長 (4〜25 B) | Code 0 (STS: 4B), Code 3 (DIB: 25B), Unit 0 (8B) |
| **READ_BLOCK** | `0x01` | 11 B | なし (0 B) | 2 B | 512 B | ブロック番号は `packet[6..8]`. 応答は `[seq, status, data(512B)]` |
| **WRITE_BLOCK**| `0x02` | 11 B | 512 B (`packet[11..522]`) | 2 B | なし (0 B) | ブロック番号 `packet[6..8]`, データ 512B を後続送信 |
| **FORMAT** | `0x03` | 11 B | なし (0 B) | 2 B | なし (0 B) | ディスク初期化 |
| **CONTROL** | `0x04` | 11 B | **可変長 (要検証)** | 2 B | なし (0 B) | **※後述の不一致・要検証項目参照** |
| **OPEN** | `0x06` | 11 B | なし (0 B) | 2 B | なし (0 B) | キャラクタ/ネットワークデバイスオープン |
| **CLOSE** | `0x07` | 11 B | なし (0 B) | 2 B | なし (0 B) | デバイスクローズ |
| **READ** | `0x08` | 11 B | なし (0 B) | 2 B | 要求長バイト分 | バイト数 `packet[6..7]`, アドレス `packet[8..10]` |
| **WRITE** | `0x09` | 11 B | 要求長バイト分 (`packet[11..]`) | 2 B | なし (0 B) | バイト数 `packet[6..7]`, アドレス `packet[8..10]` |

### パケット構造の詳細
- **リクエスト共通ヘッダ (11バイト)**:
  - `packet[0]`: `sequence_number` (シーケンス番号、0〜255)
  - `packet[1]`: `command` (コマンドコード)
  - `packet[2]`: `param_count` (パラメータ数)
  - `packet[3]`: `device_id` / `unit` (ユニット番号)
  - `packet[4..5]`: リストポインタ / 未使用 (0 パディング)
  - `packet[6..10]`: コマンド固有パラメータ（ステータスコード、ブロック番号 3B、バイトカウント等）
- **レスポンス共通ヘッダ (2バイト)**:
  - `data[0]`: `sequence_number` (リクエストと同一の番号)
  - `data[1]`: `status` (0 = `SP_SUCCESS`, 非0 = エラーコード)

### CONTROL コマンドに関する不一致の解消（検証完了・対応済）
- **AppleWin / FujiNet `Request.cpp:L48-L52` の実装**:
  ```cpp
  case CMD_CONTROL: {
    // +2 for control list length bytes we need to skip
    std::vector<uint8_t> payload(packet.begin() + 11+2, packet.end());
    request = std::make_unique<ControlRequest>(packet[0], packet[2], packet[3], packet[6], packet[7], payload);
    break;
  }
  ```
  ヘッダ 11 バイトの後、さらに「2 バイトの長さヘッダ（Control List Length）」が存在することを前提としてスキップ（`packet.begin() + 13`）しています。
- **A2retroNET `slip.c` の旧実装における不一致**:
  旧コードでは `skip = 2; // Skip SLIP_I_CTRL_CNT` により、送信側で 2 バイトの長さカウンタをスキップして送信していました。
  この結果、ペイロード長 0 の CONTROL コマンド（`MOUNT_HOST` 等）を受信した際に FujiNet-PC が `packet.size() == 11` であるにもかかわらず `packet.begin() + 13` を参照し、`packet.begin() + 13 > packet.end()` による iterator 境界超過で `std::length_error: vector` / `Abort trap: 6` クラッシュを引き起こしていました。
- **対応内容（解決済）**:
  `slip.c` の `send_request()` において `skip = 0` とし、`size += ... + 2;` に変更。FujiNet-PC / AppleWin の期待通りに 2 バイトの長さヘッダ（`SP_I_CTRL_CNT`）を含めてワイヤへ送信するように整合化しました。これによりペイロード長 0 でも常に 13 バイト以上が送信され、クラッシュが完全に解消されました。

---

## 4. ハードウェア差異と CMake / ビルド構成

### 4.1 ボード仕様比較（一次ソース裏付け）

| 項目 | A2Pico2Lite W | A2Pico2Lite（非W generic） | 根拠ソース |
| :--- | :--- | :--- | :--- |
| **`PICO_BOARD`** | `a2pico2litew` | `a2pico2-generic` | [CMakeLists.txt:L6-L12](file:///Users/oshuno/dev/a2picoFN/CMakeLists.txt#L6-L12) |
| **MCU パッケージ** | RP2350A (30ピン) | RP2350B (48ピン) / RP2350A | `a2pico2litew.h:L19`, `a2pico2-generic.h:L21` |
| **オンボード Flash**| 4 MB | 2 MB | `a2pico2litew.h:L40`, `a2pico2-generic.h:L48` |
| **Wi-Fi モジュール**| CYW43439 搭載 | 未搭載 | ハードウェア仕様 |
| **バス信号方式** | `HAVE_ENBL` (ENBL+RESET) | 個別線 (DEVSEL, IOSEL, IOSTRB) | `a2pico2.c:L75-L125`, `a2pico2.pio:L34-L44` |
| **PIO / SM 消費** | PIO0: 4 SM のみ (PIO1/2 空き) | PIO0: 4 SM + PIO1: 3 SM (計 7 SM) | `a2pico2.c:L74, L110` |
| **ステータス LED** | なし (`-1`) または GPIO 28 | GPIO 25 | `a2pico2.c:L139-L147` |

### 4.2 ボード variant と Transport の独立選択設計（将来計画・未実装）
- **将来のビルドオプション設計案（未実装）**:
  - `PICO_BOARD`: `a2pico2litew` または `a2pico2-generic`（※CMakeLists.txt に実装済み）
  - `TRANSPORT`: `CDC` (デフォルト) または `WIFI`（※将来拡張時の設計提案）
- **将来のエラーチェックロジック案**:
  ```cmake
  if(TRANSPORT STREQUAL "WIFI" AND NOT PICO_BOARD STREQUAL "a2pico2litew")
      message(FATAL_ERROR "Wi-Fi transport is only supported on A2Pico2Lite W (a2pico2litew). Board '${PICO_BOARD}' has no Wi-Fi hardware.")
  endif()
  ```
- **CDC 暫定版の共通ビルド（現状の基盤）**:
  - 現状の構成では Wi-Fi SDK（`pico_cyw43_arch`）はリンクせず、`tinyusb_device` のみを使用するため、両ボードとも共通のソースコードでクリーンにビルド可能。

---

## 5. 最小縦切り実装案と設計上のリスク評価

### 5.1 最小縦切り実装案（Phase 1: USB CDC ブリッジ）
1. **実機カード側**:
   - A2retroNET の SmartPort ROM (`6502/SMARTPORT.S`) および Core1 バスフロントエンド (`board.c`) をそのまま使用。
   - `sp_task()` 内のローカル SD ストレージ処理 (`hdd_*`) をバイパスし、すべての SmartPort 要求（INIT, STATUS, READ_BLOCK, WRITE_BLOCK）を直ちに `slip_command()` へ転送。
   - `ser_task()` の CDC 読み書きを停止し、CDC Interface 0 を SLIP 専用とする。
2. **ホスト PC 側**:
   - FujiNet-PC を `SLIP_PROTOCOL=COM` でビルドして実行（または TCP-CDC 中継スクリプトを介して `SLIP_PROTOCOL=NET` に接続）。
3. **検証項目**:
   - Apple II 起動時に ProDOS が SmartPort ドライブを認識し、ブロック 0（ブートセクタ）を読み出してブートプロセスが進むことを確認。

### 5.2 Core1 リアルタイム処理とネットワーク待ちの分離評価
- **構造的評価**:
  - Core1 は Apple II のアドレスバスとデータバスの応答（1μs周期）に専念しており、`sp_control` と `sp_buffer` の読み書きのみを行います。
  - Core0 が USB CDC や Wi-Fi ネットワークの遅延で数十ミリ秒待機している間も、Core1 の `board()` ループは一切ブロックされません。
  - 6502 CPU 側は `$CFF1` をポーリングして完了を待機するため、バスタイミング違反によるシステムハングは発生しません。
- **リスクと対策**:
  - **ProDOS / SmartPort タイムアウト**:
    ホスト側（FujiNet-PC）の応答が極端に遅い場合（数秒以上）、Apple II 側の OS ドライバが I/O エラーと判定する可能性があります。
    Core0 側で適切なタイムアウト（例: 2000ms）を設け、タイムアウト時は `SP_BUSERR (0x06)` を速やかに返して 6502 に制御を戻すフェイルセーフが必要です。

### 5.3 `gen_read_block()` JIT 6502 コード生成と `firmware[]` メモリ安全性検証（Phase 1-3 静的解析完了）

Apple II 未接続状態での READ_BLOCK 動作確認に伴い、Core0 が JIT 生成する 6502 マシン語コードが `firmware[]` 配列や Apple II 拡張 ROM のバンク予約境界を破壊しないか、静的解析による定量的検証を実施しました。

#### 1. 実行アーキテクチャと制御フロー
- Apple II 側 SmartPort ドライバ（`6502/SMARTPORT.S`）がブロック読み取り時に `JSR RDBLOCK`（`6502/SSC.CF00.S:L48-L56`）を呼び出します。
- `RDBLOCK` の動作シーケンス:
  1. `LDA #$02; STA BANKSET` ($CFFC): 拡張 ROM を **BANK 2** に切り替え。
  2. `JSR $C800`: BANK 2 に生成された Page 0（256バイト）転送ルーチンを実行。
  3. `LDA #$03; STA BANKSET` ($CFFC): 拡張 ROM を **BANK 3** に切り替え。
  4. `JSR $C800`: BANK 3 に生成された Page 1（256バイト）転送ルーチンを実行。
  5. `LDA #$01; STA BANKSET` ($CFFC): 拡張 ROM を **BANK 1**（SmartPort ファームウェア）へ復帰。
  6. `RTS`: 呼び出し元へリターン。

#### 2. JIT コード生成アルゴリズム (`gen.c:gen_read_page()`)
512 バイトブロックを 2 つの 256 バイトページに分割し、それぞれアンロールされた 6502 転送命令列を生成します。
- `data == 0x00`: 初回のみ `LDX #$00`（2B）、以降は `STX $addr`（3B: $8E lo hi）のみ。
- `data == 0x80`: 初回のみ `LDY #$80`（2B）、以降は `STY $addr`（3B: $8C lo hi）のみ。
- その他の値: 前バイトと値が異なる場合のみ `LDA #data`（2B: $A9 data）、同一値連続時は省略し `STA $addr`（3B: $8D lo hi）のみ。
- ページ末尾: `RTS`（1B: $60）を出力。

#### 3. 生成コード長の定量計算
- **最小コード長（Best Case）**:
  全 256 バイトが同一値（例: 全 0x00、全 0x80、または同一データ連続）の場合:
  - 初期化ロード: 2 バイト（`LDX`/`LDY`/`LDA`）
  - ストア命令: 256 × 3 バイト = 768 バイト
  - 終端 `RTS`: 1 バイト
  - **合計最小サイズ**: **771 バイト**
- **最大コード長（Worst Case / Theoretical Maximum）**:
  全 256 バイトが非0・非$80であり、かつ連続する同一バイトが一切存在しない最悪ケース（毎バイトでロードとストアが発生）:
  - ロード命令: 最大 256 × 2 バイト = 512 バイト（`LDA #data`）
  - ストア命令: 256 × 3 バイト = 768 バイト（`STA $addr`）
  - 終端 `RTS`: 1 バイト
  - **合計最大サイズ**: **1,281 バイト**

#### 4. メモリマップ境界と安全性証明
`firmware[]`（`incbin.S` でリンクされる `firmware.rom`、サイズ 14,336 バイト = 0x3800）のレイアウト:
- `OFFSET_NORMAL` (0x0000..0x0FFF, 4,096B): 通常スロット ROM
- `OFFSET_SERIAL` (0x1000..0x1FFF, 4,096B): SSC シリアル ROM
- `OFFSET_BANK_1` (0x2000..0x27FF, 2,048B): **SmartPort ROM コード**（`SMARTPORT.S` + `SSC.CF00.S`）
- `OFFSET_BANK_2` (0x2800..0x2FFF, 2,048B): Page 0 JIT バンク（`.RES $CF00-$C800` = 1,792B + `SSC.CF00.S` 256B）
- `OFFSET_BANK_3` (0x3000..0x37FF, 2,048B): Page 1 JIT バンク（`.RES $CF00-$C800` = 1,792B + `SSC.CF00.S` 256B）

**境界安全性判定**:
1. **BANK 1 の保護**:
   `gen_read_block()` は `OFFSET_BANK_2` ($2800) および `OFFSET_BANK_3` ($3000) のみを書き込み対象とします。BANK 1（$2000〜$27FF）は完全にアドレス範囲外であり、SmartPort ROM コードは 1 バイトたりとも破壊されません。
2. **予約領域（1,792B）に対するマージン**:
   各バンク内の JIT 領域は `$C800`〜`$CEFF`（1,792 バイト）として `.RES` 確保されています。
   最悪ケースのコード長 **1,281 バイト** に対しても、`1,792 - 1,281 = 511 バイト` のヘッドルームが存在します。
3. **`SSC.CF00.S`（$CF00）および配列終端の保護**:
   コード生成は決して `$CF00`（バンク切り替えハンドラ群）に到達せず、また `firmware[]` の配列終端（0x3800）に対しても 767 バイト手前で確実に停止します。

したがって、`gen_read_block()` による JIT 展開は数学的・構造的に完全なメモリ安全性が保証されています。

