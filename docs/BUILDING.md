# Building DriverPack Modern

The application is C++11/Qt Widgets. It embeds static Qt, SQLite, the LZMA SDK and torrent transport; it does **not** run the original DriverPack JavaScript or link the Snappy matching engine.

## Toolchain

- Windows; Python 3 and Git.
- MinGW 4.9.2 x86 (set `DRIVERPACK_MINGW_BIN` to its `bin` directory).
- Static Qt 5.6.3 with SVG support and the XP deployment target.
- Static libtorrent 1.2.20/Boost, provided by the companion SDK's `torrent-link.pri`.
- An x64 helper compiler: Visual Studio C++ tools, or `SDIO_MINGW64_BIN`.

The pinned companion [Snappy Modern v0.23.9 source](https://github.com/Diagoo1/Snappy-Driver-Installer-Modern/tree/v0.23.9) contains the dependency locks and preparation/build scripts under `tools/integration`. Follow its build instructions to prepare `vendor/qt-static` and `build-torrent-static`. Only its reusable toolchain/transport is consumed here; no SDI/SDIO executable is needed.

```powershell
git clone https://github.com/Diagoo1/DriverPack-Modern.git
cd DriverPack-Modern
.\BUILD.cmd -Action Build -SdkRoot C:\Dev\Snappy-Driver-Installer-Modern
.\BUILD.cmd -Action Test -SdkRoot C:\Dev\Snappy-Driver-Installer-Modern
```

`Build` generates `native/install64.exe` from the included CRT-free helper source, verifies/downloads Satoshi from its official publisher and embeds it, then creates `release/DriverPackModern.exe` and `release/Clean DriverPack.cmd`. Cairo is bundled under the SIL OFL. Raw Satoshi font files are deliberately excluded from Git and source archives; see their license/provenance in `assets/fonts`.

`Test` runs six bounded fixture suites plus a local-only peer transport suite, without installation or public Internet requests. `Probe` reads a real library and extracts a candidate into `build/`; it does not install. `Preview` creates private UI captures from a real library: **do not publish those captures without checking personal information**.

`--capture-gallery` is an isolated developer-only sample mode, requiring `DRIVERPACK_MODERN_TEST_SETTINGS`; it cannot install drivers. Its screenshots illustrate the interface and do not prove a successful system operation.

Windows XP through 11 and x86/x64 remain deployment targets awaiting a complete VM/device validation matrix. Import auditing is not a runtime compatibility certificate.
