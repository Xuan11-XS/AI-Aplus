#!/usr/bin/env bash
# Aplus 一键启动脚本（Arch / Ubuntu 通用）
# 用法: ./aplus.sh [--rebuild|--test|--install-deps|--shortcut|--help]
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

usage() {
    cat <<EOF
用法: ./aplus.sh [选项]

  无参数          检查依赖，需要就编译，然后启动 Aplus
  --rebuild       删掉 build/ 后重新编译（换机器或换 Qt 版本时用）
  --test          编译并跑一遍 ctest
  --install-deps  按发行版自动安装依赖（需要 sudo）
  --shortcut      安装/更新桌面快捷方式（调 install-shortcut.sh）
  --help          显示本帮助
EOF
}

CLEAN_BUILD=0
ACTION=run

while [ $# -gt 0 ]; do
    case "$1" in
        --rebuild) CLEAN_BUILD=1 ;;
        --test) ACTION=test ;;
        --install-deps) ACTION=install-deps ;;
        --shortcut) ACTION=shortcut ;;
        --help|-h) usage; exit 0 ;;
        *) echo "未知参数: $1" >&2; usage; exit 1 ;;
    esac
    shift
done

detect_distro() {
    if [ -f /etc/arch-release ]; then
        echo arch
    elif grep -q '^ID=ubuntu' /etc/os-release 2>/dev/null; then
        echo ubuntu
    else
        echo unknown
    fi
}

# 用 CMake 直接探 Qt6：和真正编译用的是同一套检测，
# 不依赖 pkg-config（Ubuntu 上 pkg-config 可能没装，或 .pc 路径不同，会误报）
qt_ok() {
    local dir
    dir="$(mktemp -d)" || return 1
    cat > "$dir/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(aplus_qt_probe LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
EOF
    if cmake -S "$dir" -B "$dir/build" >/dev/null 2>&1; then
        rm -rf "$dir"
        return 0
    fi
    rm -rf "$dir"
    return 1
}

missing_deps() {
    local missing=()
    command -v g++ >/dev/null 2>&1 || missing+=(g++)
    command -v cmake >/dev/null 2>&1 || missing+=(cmake)
    command -v ninja >/dev/null 2>&1 || missing+=(ninja)
    if command -v cmake >/dev/null 2>&1 && ! qt_ok; then
        missing+=("qt6-base-dev（Qt6 Widgets/Network 开发包）")
    fi
    printf '%s\n' "${missing[@]}"
}

install_deps() {
    local distro
    distro="$(detect_distro)"
    case "$distro" in
        arch)
            sudo pacman -S --needed base-devel cmake ninja qt6-base
            ;;
        ubuntu)
            sudo apt-get install -y build-essential cmake ninja-build pkg-config qt6-base-dev
            ;;
        *)
            echo "认不出这个发行版。手动装齐：g++、cmake、ninja，以及 Qt6 Widgets/Network 开发包。" >&2
            exit 1
            ;;
    esac
}

check_deps() {
    local missing
    missing="$(missing_deps)"
    if [ -n "$missing" ]; then
        echo "缺少依赖: $missing" >&2
        echo "可以自动装（要 sudo）: ./aplus.sh --install-deps" >&2
        exit 1
    fi
}

# 缓存里的源码路径和当前目录不一致（比如把别的机器的 build/ 拷过来了）
stale_cache() {
    [ -f build/CMakeCache.txt ] || return 1
    local cached_src
    cached_src="$(sed -n 's|^CMAKE_HOME_DIRECTORY:INTERNAL=||p' build/CMakeCache.txt | head -1)"
    [ -n "$cached_src" ] && [ "$cached_src" != "$PWD" ]
}

build() {
    check_deps
    if [ "$CLEAN_BUILD" = 1 ] || stale_cache; then
        echo "==> 清理 build/（旧缓存来自别的目录或要求强制重编）"
        rm -rf build
    fi
    if [ ! -f build/CMakeCache.txt ] || [ ! -x build/aplus ]; then
        echo "==> 配置编译环境"
        cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    fi
    echo "==> 编译"
    cmake --build build
}

case "$ACTION" in
    install-deps)
        install_deps
        echo "依赖装完了。"
        ;;
    shortcut)
        check_deps
        ./install-shortcut.sh
        ;;
    test)
        build
        echo "==> 测试"
        ctest --test-dir build --output-on-failure
        ;;
    run)
        build
        echo "==> 启动 Aplus"
        exec ./build/aplus
        ;;
esac
