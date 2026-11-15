#!/bin/sh
# Packages for the launcher and the Linux engine.
# Debian and Ubuntu use apt. CachyOS and Arch use pacman. Fedora uses dnf.
# Bazzite is an immutable Fedora: run this inside a distrobox, or install the same dnf packages there.
set -e
if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential clang cmake ninja-build pkg-config curl ca-certificates \
        libwebkit2gtk-4.1-dev libgtk-3-dev libayatana-appindicator3-dev librsvg2-dev \
        libx11-dev libvulkan-dev vulkan-tools libasound2-dev libfreetype6-dev fonts-dejavu-core patchelf
elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed --noconfirm base-devel clang cmake ninja pkgconf curl \
        webkit2gtk-4.1 gtk3 libayatana-appindicator librsvg libx11 vulkan-devel vulkan-tools alsa-lib freetype2 ttf-dejavu
elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y gcc gcc-c++ clang cmake ninja-build pkgconf-pkg-config curl \
        webkit2gtk4.1-devel gtk3-devel libappindicator-gtk3-devel librsvg2-devel \
        libX11-devel vulkan-loader-devel vulkan-tools alsa-lib-devel freetype-devel dejavu-sans-fonts
else
    echo "No apt-get, pacman or dnf on this machine." >&2
    exit 1
fi
if ! command -v cargo >/dev/null 2>&1; then
    curl --proto '=https' --tlsv1.2 -fsSL https://sh.rustup.rs | sh -s -- -y --default-toolchain stable
fi
echo "Ready. Engine: sh patches/apply.sh && cmake --preset linux-x64 && cmake --build build/linux-x64 --target z_generals"
echo "Launcher: make linux-client"
