# Kana–Kanji Conversion (C++)

## 日本語

このリポジトリは、Mozc の辞書データ（`dictionary00.txt`〜`dictionary09.txt` および `connection_single_column.txt`）を取得し、**かな（ひらがな）入力から「かな漢字交じり」候補を生成するための C++ 実装**を提供します。

内部では UTF-16 ベースの LOUDS（Level-Order Unary Degree Sequence）を用いてトライ木を圧縮し、辞書ルックアップと候補生成を高速化します。

### できること

* Mozc 辞書のダウンロード（`mozc_dic_fetch`）
* 変換用アーティファクト生成

  * 読み（yomi）→ termId の LOUDS（`yomi_termid.louds`）
  * 表記（tango）の LOUDS（`tango.louds`）
  * termId → トークン列（`token_array.bin`）
  * POS テーブル（`pos_table.bin`）
  * 接続コスト（`connection_single_column.bin`）
* かな→候補のデバッグ出力（`prefix_predict_cli` / `astar_bunsetsu_cli`）
* LOUDS への common prefix search / termId 取得のデバッグ（`cps_cli`, `termid_cli`）
* （任意）AJIMEE-Bench によるベンチマーク（`tools/run_ajimee_bench.py`）

### 生成される成果物（Release で配布したい対象）

以下は「変換に必須」な最低限の成果物です。

* `yomi_termid.louds`
* `tango.louds`
* `token_array.bin`
* `pos_table.bin`
* `connection_single_column.bin`

---

## ローカルビルド手順（Ubuntu / WSL 推奨）

### 1) 依存パッケージ

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev zlib1g-dev
```

### 2) ビルド

#### (A) Mozc 辞書ダウンロード用バイナリも作成する場合（`mozc_dic_fetch` が必要）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_MOZC_FETCH=ON
cmake --build build -j
```

#### (B) 辞書ダウンロードは不要（すでに辞書ファイルがある）場合

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

#### (C) zenz のビルド

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_ZENZ=ON
cmake --build build -j
```

#### 再ビルド

```bash
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_MOZC_FETCH=ON -DENABLE_ZENZ=ON && cmake --build build -j


rm -rf build && cmake -S . -B build -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 -DCMAKE_CXX_STANDARD=20 -DBUILD_MOZC_FETCH=ON -DENABLE_ZENZ=ON && cmake --build build -j
```

生成される主なバイナリ：

* `build/mozc_dic_fetch`（`-DBUILD_MOZC_FETCH=ON` のとき）
* `build/dictionary_builder`
* `build/tries_token_builder`
* `build/prefix_predict_cli`
* `build/astar_bunsetsu_cli`
* `build/astar_bunsetsu_parallel_cli`
* `build/cps_cli`
* `build/termid_cli`

> メモ：GCC のバージョン固定（例: `gcc-10`）をしたい場合は、あなたの環境に合わせて
> `-DCMAKE_C_COMPILER=... -DCMAKE_CXX_COMPILER=...` を追加してください。

例：

```bash
cmake -S . -B build -DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10 -DCMAKE_CXX_STANDARD=20
```
---

## 辞書アーティファクト生成（推奨フロー）

### 1) Mozc 辞書データの取得

```bash
./build/mozc_dic_fetch
```

デフォルトでは `src/dictionary_builder/mozc_fetch/` に以下が保存されます：

* `dictionary00.txt`〜`dictionary09.txt`
* `connection_single_column.txt`

### 2) 接続コスト（connection）をバイナリ化

```bash
./build/dictionary_builder --in src/dictionary_builder/mozc_fetch --out build/mozc_reading.louds --conn-out build/connection_single_column.bin
```

メモ：

* `--out`（読み LOUDS）は検証用途です（変換本体の必須要件は上記 5 ファイルです）。
* `connection_single_column.txt` の 1 行目をスキップする実装になっている場合があります（Mozc 形式に合わせた処理）。

### 3) 変換用アーティファクト生成（yomi/tango/token/POS）

```bash
./build/tries_token_builder --in_dir src/dictionary_builder/mozc_fetch --out_dir build
```

`build/` に以下が生成されます：

* `yomi_termid.louds`
* `tango.louds`
* `token_array.bin`
* `pos_table.bin`

---

## かな→候補（デバッグ出力）

### prefix_predict_cli（簡易）

#### 単発クエリ

```bash
./build/prefix_predict_cli --yomi_termid build/yomi_termid.louds --tango build/tango.louds --tokens build/token_array.bin --q "きょうはいいてんきです" --limit 10
```

#### 標準入力（パイプ）

```bash
echo "きょうはいいてんきです" | ./build/prefix_predict_cli --yomi_termid build/yomi_termid.louds --tango build/tango.louds --tokens build/token_array.bin --stdin --limit 10
```

### astar_bunsetsu_cli（候補生成 + 接続コスト）

#### CommonPrefixSearch（文節候補）

```bash
./build/astar_bunsetsu_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --q きょうのてんき --n 10 --beam 50 --show_bunsetsu
```

#### CommonPrefixSearch + 修飾キー省略（例: `cps_omit`）

```bash
./build/astar_bunsetsu_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --q かんし --n 10 --beam 50 --show_bunsetsu --yomi_mode cps_omit
```

#### 予測表示（例）

```bash
./build/astar_bunsetsu_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --q あいあ --n 10 --beam 50 --show_bunsetsu --show_prediction --pred_n 8
```

#### parallel 版（例）

```bash
./build/astar_bunsetsu_parallel_cli --yomi_termid build/yomi_termid.louds --tango build/tango.louds --tokens build/token_array.bin --pos_table build/pos_table.bin --conn build/connection_single_column.bin --q "きょう" --n 4 --beam 20 --show_bunsetsu --show_omit
```

メモ：

* `prefix_predict_cli` は現状「挙動確認・デバッグ向け」に詳細ログを出力します。
* `--no_dedup` を付けると、同一文字列候補の重複排除を無効化できます。

### zenz

```bash

./build/zenz_convert_cli --model ./models/zenz/zenz-v3.1-small.gguf --q "わたしのなまえはなかのです" --max_new 64 --verbose

./build/zenz_convert_cli --model ./models/zenz/zenz-v3.1-small.gguf --mode eval --input "わたしのなまえはなかのです" --candidate "私の名前は中野です" --candidate "わたしのなまえはかのです" --candidate "私の名前はなかのです" --n_ctx 512 --n_batch 512 --verbose

# 既存の変換候補に zenz の変換候補を追加する
./build/astar_zenz_fuse_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --zenz_model ./models/zenz/zenz-v3.1-small.gguf --q "きょうのてんき" --n 10 --beam 50 --show_bunsetsu 2>/dev/null

./build/astar_zenz_fuse_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --zenz_model ./models/zenz/zenz-v3.1-small.gguf --q "わたしのなまえはなかのです" --zenz_mode eval --zenz_show_eval --verbose 2>/dev/null


./build/astar_zenz_fuse_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --zenz_model ./models/zenz/zenz-v3.1-small.gguf --q "きをきる" --zenz_mode gen_eval --zenz_show_eval --verbose 2>/dev/null --yomi_mode cps_omit --beam 16 --typo on --typo_max_penalty 2 --typo_weight 1500 --typo_max_out 128 

```

### Typo Correction 12 key:
```bash

 ./build/astar_zenz_fuse_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --zenz_model ./models/zenz/zenz-v3.1-small.gguf --q "あはようございます" --zenz_mode eval --zenz_show_eval --verbose 2>/dev/null --yomi_mode cps_omit --beam 16 --typo on --typo_max_penalty 2 --typo_weight 1500 --typo_max_out 128

```

```bash
./build/astar_zenz_fuse_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --zenz_model ./models/zenz/zenz-v3.1-small.gguf --q "こはようございます" --zenz_mode eval --zenz_show_eval --verbose 2>/dev/null --yomi_mode cps_omit --beam 16 --typo on --typo_max_penalty 2 --typo_weight 1500 --typo_max_out 128

./build/astar_zenz_fuse_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --zenz_model ./models/zenz/zenz-v3.1-small.gguf --q "かはようございます" --zenz_mode eval --zenz_show_eval --verbose 2>/dev/null --yomi_mode cps_omit --beam 16 --typo on --typo_max_penalty 2 --typo_weight 1500 --typo_max_out 128
```
---

## AJIMEE-Bench ベンチマーク

あなたの `astar_bunsetsu_cli` を **「外部IME候補生成器」**として呼び出し、AJIMEE-Bench の評価データで **Acc@1 / Acc@N / Acc@K / MRR@K / MinCER@1** を計測できます。

### 1) AJIMEE-Bench を submodule として追加

```bash
git submodule add https://github.com/azooKey/AJIMEE-Bench.git AJIMEE-Bench
git submodule update --init --recursive
```

評価データ（代表）：

* `AJIMEE-Bench/JWTD_v2/v1/evaluation_items.json`

### 2) ベンチスクリプト

このリポジトリ側に `tools/run_ajimee_bench.py` を置いて実行します。

* **進捗表示**：`--progress`（`--every` で間引き）
* **失敗のみ JSONL 出力**：`--fail_jsonl`
* **どの条件を「失敗」とみなすか**：`--fail_mode`

  * `acc1`：Acc@1 が外れた
  * `accn`：Acc@N（N は `--n`）が外れた（Top-N に正解が無い）
  * `either`：acc1 または accn が外れた（デフォルト）
  * `both`：acc1 も accn も外れた（= 実質「Top-N に正解が無い」だけ拾う用途に便利）
  * `error`：タイムアウトや exit!=0、パース失敗など“実行エラーのみ”

> 注意：AJIMEE-Bench の `input` はカタカナであるため、スクリプト側で **カタカナ→ひらがな**へ変換して `astar_bunsetsu_cli` に渡します。

### 3) 実行例（文脈なし 100件）

#### (A) 1位品質を見たい（Acc@1 miss を失敗として JSONL 出力）

```bash
python3 tools/run_ajimee_bench.py --items AJIMEE-Bench/JWTD_v2/v1/evaluation_items.json --subset no_context --n 10 --beam 50 --k 10 --timeout 10 --progress --every 10 --fail_jsonl failed_acc1.jsonl --fail_mode acc1 --cmd './build/astar_bunsetsu_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --q "{q}" --n {n} --beam {beam} --show_bunsetsu'
```

#### (B) Top-N 取りこぼしだけ拾いたい（候補生成器の欠落分析）

```bash
python3 tools/run_ajimee_bench.py --items AJIMEE-Bench/JWTD_v2/v1/evaluation_items.json --subset no_context --n 10 --beam 50 --k 10 --timeout 10 --progress --every 10 --fail_jsonl failed_missing_in_topN.jsonl --fail_mode accn --cmd './build/astar_bunsetsu_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --q "{q}" --n {n} --beam {beam} --show_bunsetsu'
```

#### (C) 実行エラーだけ拾う（timeout/クラッシュ/パース失敗）

```bash
python3 tools/run_ajimee_bench.py --items AJIMEE-Bench/JWTD_v2/v1/evaluation_items.json --subset no_context --n 10 --beam 50 --k 10 --timeout 10 --progress --every 10 --fail_jsonl failed_errors_only.jsonl --fail_mode error --cmd './build/astar_bunsetsu_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --q "{q}" --n {n} --beam {beam} --show_bunsetsu'
```

### 4) 文脈あり（100件）について

AJIMEE-Bench は「文脈あり」サブセットも持ちますが、現在の CLI が文脈を入力として使わない場合、結果は参考値になります（将来的に `--left/--right` 等で対応可能）。

---

## LOUDS ツール（デバッグ）

### Common Prefix Search（UTF-8 入力 → 一致したキー列を表示）

```bash
./build/cps_cli --louds build/yomi_termid.louds --q "きょうはいいてんきです"
```

### TermId 取得（UTF-8 入力 → nodeIndex / termId などを表示）

```bash
./build/termid_cli --louds build/yomi_termid.louds --q "きょう"
```

---

## ライセンス

* **プログラム本体**：MIT License（`LICENSE`）
* **辞書データ**：Mozc のライセンス（`LICENSE-MOZC`）

  * 本リポジトリは Mozc の辞書データそのものを同梱せず、`mozc_dic_fetch` により取得します。
* **ベンチマーク（AJIMEE-Bench）**：AJIMEE-Bench のライセンスに従ってください（submodule として取得し、本リポジトリに同梱しない想定）

---

## English

This repository provides a C++ pipeline for **Kana (Hiragana) → Kana–Kanji mixed conversion** using dictionary data from Mozc (`dictionary00.txt`–`dictionary09.txt` and `connection_single_column.txt`).

Internally, it uses UTF-16 based LOUDS (Level-Order Unary Degree Sequence) to compress tries and accelerate lookups and candidate generation.

### What you get

* Download Mozc dictionary data (`mozc_dic_fetch`)
* Build conversion artifacts:

  * Yomi (reading) → termId LOUDS (`yomi_termid.louds`)
  * Tango (surface form) LOUDS (`tango.louds`)
  * termId → token postings (`token_array.bin`)
  * POS table (`pos_table.bin`)
  * Connection costs (`connection_single_column.bin`)
* Debug conversion output (`prefix_predict_cli` / `astar_bunsetsu_cli`)
* Debug LOUDS CPS / termId operations (`cps_cli`, `termid_cli`)
* (Optional) Benchmark with AJIMEE-Bench (`tools/run_ajimee_bench.py`)

### Release artifacts (minimum required for conversion)

* `yomi_termid.louds`
* `tango.louds`
* `token_array.bin`
* `pos_table.bin`
* `connection_single_column.bin`

---

## Build (Ubuntu / WSL recommended)

### 1) Dependencies

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev zlib1g-dev
```

### 2) Build

#### (A) Build including Mozc fetcher (`mozc_dic_fetch`)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_MOZC_FETCH=ON
cmake --build build -j
```

#### (B) Build without fetcher

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Key binaries:

* `build/mozc_dic_fetch` (when enabled)
* `build/dictionary_builder`
* `build/tries_token_builder`
* `build/prefix_predict_cli`
* `build/astar_bunsetsu_cli`
* `build/astar_bunsetsu_parallel_cli`
* `build/cps_cli`
* `build/termid_cli`

---

## Generate dictionary artifacts (recommended flow)

### 1) Fetch Mozc dictionary files

```bash
./build/mozc_dic_fetch
```

By default, files are stored under `src/dictionary_builder/mozc_fetch/`:

* `dictionary00.txt`–`dictionary09.txt`
* `connection_single_column.txt`

### 2) Build connection cost binary

```bash
./build/dictionary_builder --in src/dictionary_builder/mozc_fetch --out build/mozc_reading.louds --conn-out build/connection_single_column.bin
```

### 3) Build LOUDS tries + token array

```bash
./build/tries_token_builder --in_dir src/dictionary_builder/mozc_fetch --out_dir build
```

Outputs in `build/`:

* `yomi_termid.louds`
* `tango.louds`
* `token_array.bin`
* `pos_table.bin`

---

## Kana → candidates (debug)

```bash
./build/astar_bunsetsu_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --q きょうのてんき --n 10 --beam 50 --show_bunsetsu
```

---

## Benchmark with AJIMEE-Bench

You can benchmark your generator (`astar_bunsetsu_cli`) as an external IME candidate provider using AJIMEE-Bench.

### 1) Add AJIMEE-Bench as a submodule

```bash
git submodule add https://github.com/azooKey/AJIMEE-Bench.git AJIMEE-Bench
git submodule update --init --recursive
```

Dataset (example):

* `AJIMEE-Bench/JWTD_v2/v1/evaluation_items.json`

### 2) Run the benchmark script

The script supports progress reporting and failure-only dumps:

* `--progress` / `--every`
* `--fail_jsonl` + `--fail_mode` (`acc1`, `accn`, `either`, `both`, `error`)

Example (no_context, 100 items):

```bash
python3 tools/run_ajimee_bench.py --items AJIMEE-Bench/JWTD_v2/v1/evaluation_items.json --subset no_context --n 10 --beam 50 --k 10 --timeout 10 --progress --every 10 --fail_jsonl failed_missing_in_topN.jsonl --fail_mode accn --cmd './build/astar_bunsetsu_cli --yomi_termid ./build/yomi_termid.louds --tango ./build/tango.louds --tokens ./build/token_array.bin --pos_table ./build/pos_table.bin --conn ./build/connection_single_column.bin --q "{q}" --n {n} --beam {beam} --show_bunsetsu'
```

Note: AJIMEE-Bench inputs are Katakana; the script converts them to Hiragana before calling your CLI.

---

## License

* **Code**: MIT License (`LICENSE`)
* **Dictionary data**: Mozc license (`LICENSE-MOZC`)

  * This repository does not bundle Mozc dictionary data; it is fetched via `mozc_dic_fetch`.
* **Benchmark**: Follow AJIMEE-Bench license terms (recommended as a submodule, not vendored).
