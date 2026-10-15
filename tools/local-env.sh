#!/usr/bin/env bash
# Optional wrapper for development tools extracted under .local-tools/root.
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
task_sdk="$task_root/.local-tools/root"
if [[ ! -x "$task_sdk/usr/bin/cmake" ]]; then
    echo 'No local tool extraction found; use the system setup command in README.md.' >&2
    exit 1
fi
export PATH="$task_sdk/usr/bin:$task_sdk/usr/lib/llvm-21/bin:$PATH"
export LD_LIBRARY_PATH="$task_sdk/usr/lib/x86_64-linux-gnu:$task_sdk/usr/lib/llvm-21/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CMAKE_PREFIX_PATH="$task_sdk/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export PKG_CONFIG_SYSROOT_DIR="$task_sdk"
export PKG_CONFIG_LIBDIR="$task_sdk/usr/lib/x86_64-linux-gnu/pkgconfig:$task_sdk/usr/share/pkgconfig"
export VK_LAYER_PATH="$task_sdk/usr/share/vulkan/explicit_layer.d"
export XKB_BINDIR="$task_sdk/usr/bin"
export LSAN_OPTIONS="suppressions=$task_root/tests/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
exec "$@"
