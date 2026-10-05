# pb - universal package builder

[![CI](https://github.com/qwarwin4/pb/actions/workflows/release.yml/badge.svg)](https://github.com/qwarwin4/pb/actions/workflows/release.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Email](https://img.shields.io/badge/email-qwarwin%40gmail.com-D14836?logo=gmail&logoColor=white)](mailto:qwarwin@gmail.com)
[![Telegram](https://img.shields.io/badge/Telegram-Channel-26A5E4?logo=telegram&logoColor=white)](https://t.me/qwarwin4git)

Builds `.deb`, `.rpm`, `.AppImage` and an AUR `PKGBUILD` from a single `build.toml`.
No `dpkg-deb`, `rpmbuild`, `appimagetool` or `mksquashfs` needed: pb writes every format itself.
Static binary, runs on any distro, x86_64 and aarch64.

## Install

```sh
git clone --depth 1 https://github.com/Qwarwin4/pb
cd pb
./install.sh
```

Or use a package: `apt install ./pb_1.0.0_amd64.deb`, `dnf install ./pb-1.0.0-1.x86_64.rpm`,
`makepkg -si` with the attached `PKGBUILD`, or run the AppImage directly.

## Usage

### 1. Describe the package

Put a `build.toml` in the project root:

```toml
name        = "hello"                        # required: lowercase, digits, + . -
version     = "1.0.0"
description = "Prints a friendly greeting"
maintainer  = "Jane Doe <jane@example.org>"
license     = "GPL-3.0-or-later"
url         = "https://github.com/jane/hello"
binary      = "hello"                        # executable name, defaults to name
depends     = ["libc6"]                      # passed to the packages as-is
```

| Key | Default | Notes |
|---|---|---|
| `name` | none | the only required key |
| `version` | `0.1.0` | must start with a digit |
| `description` | same as `name` | |
| `maintainer` | `unknown` | |
| `license` | `unspecified` | SPDX id recommended |
| `binary` | same as `name` | file installed to `/usr/bin` |
| `arch` | detected from ELF | `amd64`, `arm64`, `i386`, `armhf`, `riscv64`, `all` |
| `depends` | `[]` | runtime dependencies |

### 2. Build

```sh
pb build                       # build the project and produce all four formats
pb build path/to/project       # point at another directory
pb build --deb --rpm           # only selected formats: --deb --rpm --appimage --aur
pb build --out dist            # put everything in one folder
pb build --binary ./hello      # skip compiling, package an existing binary
```

pb recognizes the project type and builds it in release mode:

| Project | Detected by | Build command |
|---|---|---|
| CMake | `CMakeLists.txt` | `cmake -DCMAKE_BUILD_TYPE=Release` + `cmake --build` |
| Rust | `Cargo.toml` | `cargo build --release` |
| Go | `go.mod` | `go build` |
| Make | `Makefile` | `make` |

Anything else (Python, shell scripts, prebuilt binaries) goes through `--binary`.

### 3. Get the packages

```
$ pb build
package: hello 1.0.0
project type: CMake/C++
building project...
architecture: amd64
packing...
  [deb] -> ~/pb-builds/deb/hello_1.0.0_amd64.deb
  [rpm] -> ~/pb-builds/rpm/hello-1.0.0-1.x86_64.rpm
  [appimage] -> ~/pb-builds/appimage/hello-1.0.0-x86_64.AppImage
  [aur] -> ~/pb-builds/aur/hello
```

The `aur` folder contains `PKGBUILD` and `.SRCINFO`. A git project gets a `hello-git` package that builds from `origin`. Anything else gets a source tarball with a real sha256.
If any format fails, pb names it and exits with code 1.

### GUI apps

CLI tools need nothing extra. For desktop apps, add:

```toml
gui        = true
icon       = "assets/icon.png"   # .png or .svg
categories = "Utility;Development;"
```

pb adds a `.desktop` entry and installs the icon into the hicolor theme.

### Commands

| Command | What it does |
|---|---|
| `pb build [DIR]` | build packages |
| `pb install` | copy pb to `/usr/local/bin` |
| `pb uninstall [--purge]` | remove pb; `--purge` also clears config and cache |
| `pb repair` | fix permissions, broken language config and stale cache |
| `pb update [--yes]` | update from GitHub Releases, verified by sha256 |
| `pb lang ru\|en` | switch message language |

### Good to know

- **Portability.** pb warns if the binary needs a newer glibc than Debian 11, Ubuntu 20.04 or RHEL 8 ship. Build statically or on an older distro to cover them.
- **AppImage runtime.** It's downloaded once with curl or wget and cached in `~/.cache/pb/`. To build offline, run `PB_APPIMAGE_RUNTIME=/path/to/runtime pb build`.
- **Reproducible output.** File timestamps are zeroed. The rpm build time respects `SOURCE_DATE_EPOCH`.

Builds CMake, Cargo, Go and Make projects automatically. Other commands: `install`, `uninstall`, `repair`, `update`, `lang ru|en`.

Tested on Debian 11/12, Ubuntu 20.04/24.04, Fedora, Rocky 8, openSUSE Leap, Alpine and Arch.

Licensed under GPL-3.0-or-later.
