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

## zenz モデルのインストール時ダウンロード

既定設定では、モデルファイルを次の場所に配置します。

```text
%LOCALAPPDATA%\SumireIME\models\zenz-v3.1-small-gguf\ggml-model-Q5_K_M.gguf
```

インストーラーでは、インストール中に既定の `zenz` モデルをダウンロードするかどうかを選択できます。チェックを入れた場合は、インストール時に既定 repo から取得します。チェックを外した場合は、インストール後に手動で配置してください。既定の取得元は次です。

- [Miwa-Keita/zenz-v3.1-small-gguf](https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf)

設定画面では preset ごとにモデルパスと repo を変更できます。

## 配布物

GitHub Releases には次の zip をアップロードする想定です。

- `*-installer.zip`: `SumireInstaller.exe` とインストールに必要な同梱ファイル一式
- `*-binaries.zip`: 実行ファイル群と辞書ファイル一式

注意:
`SumireInstaller.exe` は単体配布を前提にしていません。インストーラーは近くにある DLL、設定アプリ、辞書などを参照して配置するため、release では zip ごと配布してください。

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
2. `scripts/package-release.ps1` で配布 zip を生成
3. GitHub Release を作成または更新し、zip を asset としてアップロード

## ベース

このプロジェクトは Microsoft の `UILess Mode Text Service` サンプルを出発点にしています。

- [Windows classic samples / uilessmode](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/input/tsf/textservice/uilessmode)
