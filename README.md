# pb - universal package builder

Builds `.deb`, `.rpm`, `.AppImage` and an AUR `PKGBUILD` from a single `build.toml`.
No `dpkg-deb`, `rpmbuild`, `appimagetool` or `mksquashfs` needed: pb writes every format itself.
Static binary, runs on any distro, x86_64 and aarch64.

## Install

```sh
curl -fLo pb https://github.com/qwarwin4/pb/releases/download/v1.0.0/pb-1.0.0-$(uname -m)
chmod +x pb && sudo ./pb install
```

Or use a package: `apt install ./pb_1.0.0_amd64.deb`, `dnf install ./pb-1.0.0-1.x86_64.rpm`,
`makepkg -si` with the attached `PKGBUILD`, or run the AppImage directly.

## Usage

```toml
name    = "hello"
version = "1.0.0"
binary  = "hello"
```

```sh
pb build                    # all formats into ~/pb-builds/
pb build --deb --rpm        # selected formats
pb build --binary ./hello   # package a prebuilt binary
```

Builds CMake, Cargo, Go and Make projects automatically. Other commands: `install`, `uninstall`, `repair`, `update`, `lang ru|en`.

Tested on Debian 11/12, Ubuntu 20.04/24.04, Fedora, Rocky 8, openSUSE Leap, Alpine and Arch.

Licensed under GPL-3.0-or-later.