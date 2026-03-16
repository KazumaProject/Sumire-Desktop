# Sumire Desktop

Sumire Desktop は Windows 向けの日本語入力 IME です。Microsoft の TSF サンプルをベースに、辞書変換と `zenz` による GGUF モデル補完を組み合わせています。

## 状態

- 現在は実験的な実装です。
- 実運用向けの安定性、互換性、インストーラー体験は継続改善中です。

## 含まれるコンポーネント

- `Sumite-Desktop.dll`: IME 本体
- `SumireSettings.exe`: 設定 UI
- `SumireZenzService.exe`: GGUF モデルを読み込む補助プロセス
- `SumireInstaller.exe`: 配布用インストーラー
- `SumireUninstaller.exe`: アンインストーラー

## ドキュメント

- [設定 README](./docs/settings/README.md)
- [基本操作 README](./docs/basic-usage/README.md)

## zenz モデルのインストール時ダウンロード

既定設定では、モデルファイルを次の場所に配置します。

```text
%LOCALAPPDATA%\SumireIME\models\zenz-v3.1-small-gguf\ggml-model-Q5_K_M.gguf
```

インストーラーでは、インストール中に既定の `zenz` モデルをダウンロードするかどうかを選択できます。チェックを入れた場合は、インストール時に既定 repo から取得します。チェックを外した場合は、インストール後に手動で配置してください。既定の取得元は次です。

- [Miwa-Keita/zenz-v3.1-small-gguf](https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf)

設定画面では preset ごとにモデルパスと repo を変更できます。

## 配布物

GitHub Releases には自己完結した `*-Setup.exe` をアップロードする想定です。

- `*-Setup.exe`: インストーラー本体に IME の実行ファイル群と辞書を内包した単体セットアップ

セットアップ時には次を選べます。

- 既定の `zenz` モデルをインストール中にダウンロードするか
- `Sumire Settings` のデスクトップショートカットを作成するか

## ローカルビルド

前提:

- Visual Studio 2022
- MSVC v145 toolset
- Git submodule を含めた checkout

手順:

```powershell
git clone --recurse-submodules <repository-url>
cd Sumire-Desktop
msbuild Sumire.sln /m /p:Configuration=Release /p:Platform=x64
```

ビルド成果物は通常 `x64\Release\` に出力されます。

## GitHub Actions リリース

`.github/workflows/release.yml` は tag push を契機に次を実行します。

1. `Sumire.sln` を `Release|x64` でビルド
2. `scripts/package-release.ps1` で `Setup.exe` を生成
3. GitHub Release を作成または更新し、`Setup.exe` を asset としてアップロード

## ベース

このプロジェクトは Microsoft の `UILess Mode Text Service` サンプルを出発点にしています。

- [Windows classic samples / uilessmode](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/input/tsf/textservice/uilessmode)

## ライセンス

- このリポジトリのソースコードは [MIT](./LICENSE) で提供します。
- このプロジェクトは Mozc の辞書データを使用しています。辞書関連ファイルと注意書きには Mozc 側のライセンスおよび辞書データの注意事項が別途適用されます。
  - [google/mozc LICENSE](https://github.com/google/mozc/blob/master/LICENSE)
- インストーラーが参照する既定の zenz モデル [Miwa-Keita/zenz-v3.1-small-gguf](https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf) は、モデルカードで `cc-by-sa-4.0` とされています。
  - [Hugging Face model card README](https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf/blob/main/README.md)

これらのサードパーティライセンスは、このリポジトリの MIT ライセンスとは別に適用されます。
