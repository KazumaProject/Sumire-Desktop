# Sumire Desktop

Sumire Desktop is a Windows Japanese IME built on top of Microsoft's TSF sample. It combines dictionary-based conversion with `zenz`-backed GGUF model assistance.

## Status

- The project is still experimental.
- Stability, compatibility, and packaging are still being refined.

## Included components

- `Sumite-Desktop.dll`: main IME module
- `SumireSettings.exe`: settings UI
- `SumireZenzService.exe`: helper process that loads GGUF models
- `SumireInstaller.exe`: distribution installer
- `SumireUninstaller.exe`: uninstaller

## Optional zenz model download during install

By default, the model file is expected at:

```text
%LOCALAPPDATA%\SumireIME\models\zenz-v3.1-small-gguf\ggml-model-Q5_K_M.gguf
```

The installer can optionally download the default model during installation. If you leave that option disabled, place the model file there manually after install. The default source is:

- [Miwa-Keita/zenz-v3.1-small-gguf](https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf)

The settings UI lets you override both the model path and the model repo. The repo can be a Hugging Face repository URL, a `resolve/...` URL, or a direct `.gguf` file URL.

## Release assets

GitHub Releases are expected to publish:

- `*-installer.zip`: `SumireInstaller.exe` plus the files it needs beside it
- `*-binaries.zip`: runtime executables and dictionary files

Note:
`SumireInstaller.exe` is not a self-contained bootstrapper. It copies adjacent payload files during installation, so releases should distribute the zip bundle rather than the installer executable alone.

## Building locally

Requirements:

- Visual Studio 2022
- MSVC v145 toolset
- A checkout with Git submodules

Steps:

```powershell
git clone --recurse-submodules <repository-url>
cd Sumire-Desktop
msbuild Sumire.sln /m /p:Configuration=Release /p:Platform=x64
```

Build outputs are typically placed in `x64\Release\`.

## GitHub Actions release flow

`.github/workflows/release.yml` runs on tag pushes and does the following:

1. Builds `Sumire.sln` in `Release|x64`
2. Generates distributable zip files with `scripts/package-release.ps1`
3. Creates or updates a GitHub Release and uploads the zip assets

## Base sample

This project started from Microsoft's `UILess Mode Text Service` sample.

- [Windows classic samples / uilessmode](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/input/tsf/textservice/uilessmode)
