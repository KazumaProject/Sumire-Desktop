# Sumire Desktop

- [日本語 README](./README.ja.md)
- [English README](./README.en.md)

Sumire Desktop is a Windows Japanese IME project based on Microsoft's `UILess Mode Text Service` sample.

Current highlights:

- The installer can optionally download the default `zenz` GGUF model during installation.
- GitHub Actions can build release assets and upload them when a tag is pushed.
- Release packages are generated as zip bundles so the installer has the payload files it requires.

License:

- Project source code: [MIT](./LICENSE)
- Mozc dictionary data: see the Mozc license linked in the language-specific READMEs
- Default zenz model (`zenz-v3.1-small-gguf`): CC-BY-SA-4.0
