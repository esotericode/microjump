#!/usr/bin/env bash
# Ubuntu 26.04 only: extract development tools without installing system packages.
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$task_root/.local-tools/debs" "$task_root/.local-tools/root"
cd "$task_root/.local-tools/debs"
apt-get download cmake cmake-data ninja-build clang-21 clang-format-21 clang-tidy-21 \
    libclang-cpp21 libllvm21 libclang-common-21-dev libclang1-21 libclang-rt-21-dev llvm-21 llvm-21-linker-tools \
    glslc libshaderc1 vulkan-validationlayers vulkan-tools libvulkan-dev \
    pkgconf-bin libpkgconf7 libwayland-dev libwayland-bin libxkbcommon-dev libx11-dev \
    libxext-dev libxrandr-dev libxrender-dev libxcursor-dev libxi-dev libxfixes-dev \
    libxss-dev libxtst-dev libdecor-0-dev x11proto-dev xorg-sgml-doctools libxau-dev \
    libxdmcp-dev libxcb1-dev libxkbcommon-x11-dev libffi-dev libegl-dev libgl-dev \
    libglx-dev libgles-dev libopengl-dev libjsoncpp26 librhash1 libxss1 \
    xvfb xauth libxfont2 x11-xkb-utils xserver-common xdotool libxdo3
for package in ./*.deb; do
    dpkg-deb -x "$package" ../root
done
cd "$task_root"
python3 - <<'PY'
from pathlib import Path
root = Path('.local-tools/root')
# Development packages' .so links normally resolve beside installed runtime libs.
# Resolve missing targets to the existing system libs without replacing those libs.
for link in (root / 'usr/lib/x86_64-linux-gnu').glob('*.so'):
    if link.is_symlink() and not link.exists():
        system = Path('/usr/lib/x86_64-linux-gnu') / link.readlink().name
        if system.exists():
            link.unlink()
            link.symlink_to(system)
for name, target in [('pkg-config', 'pkgconf'), ('clang', 'clang-21'),
                     ('clang++', 'clang++-21'), ('clang-format', 'clang-format-21'),
                     ('clang-tidy', 'clang-tidy-21')]:
    path = root / 'usr/bin' / name
    if not path.exists():
        path.symlink_to(target)
PY
echo 'Local tools ready. Prefix development commands with tools/local-env.sh.'
