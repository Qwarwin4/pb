#!/usr/bin/env bash
set -euo pipefail

dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cxx="${CXX:-g++}"

for tool in "$cxx" make; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "$tool не найден. Нужны: gcc/g++ 9+, make, заголовки zlib."
        echo "  Arch:          pacman -S --needed base-devel zlib"
        echo "  Debian/Ubuntu: apt install build-essential zlib1g-dev"
        echo "  Fedora:        dnf install gcc-c++ make zlib-devel"
        exit 1
    fi
done

if ! echo '#include <zlib.h>' | "$cxx" -E -x c++ - >/dev/null 2>&1; then
    echo "нет заголовков zlib (zlib.h)."
    echo "  Debian/Ubuntu: apt install zlib1g-dev"
    echo "  Fedora:        dnf install zlib-devel"
    exit 1
fi

echo "сборка pb..."
make -C "$dir" CXX="$cxx"

if [ "$(id -u)" -eq 0 ]; then
    "$dir/pb" install
elif command -v sudo >/dev/null 2>&1; then
    sudo "$dir/pb" install
elif command -v doas >/dev/null 2>&1; then
    doas "$dir/pb" install
else
    echo "нужны права root, sudo/doas не найдены. Запусти от root: $dir/pb install"
    exit 1
fi

echo "готово: pb -h"
