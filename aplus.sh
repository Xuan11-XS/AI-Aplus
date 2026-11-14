#!/usr/bin/env bash
# Aplus 一键启动脚本（Arch / Ubuntu 通用）
# 用法: ./aplus.sh [--rebuild|--test|--install-deps|--shortcut|--help]
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

usage() {
    cat <<EOF
用法: ./aplus.sh [选项]

  无参数          检查依赖，需要就编译，然后启动 Aplus
  --rebuild       强制重新编译
  --test          编译并跑一遍 ctest
  --install-deps  按发行版自动安装依赖（需要 sudo）
  --shortcut      安装/更新桌面快捷方式（调 install-shortcut.sh）
  --help          显示本帮助
EOF
}

FORCE_REBUILD=0
ACTION=run

while [ $# -gt 0 ]; do
    case "$1" in
        --rebuild) FORCE_REBUILD=1 ;;
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

missing_deps() {
    local missing=()
    command -v g++ >/dev/null 2>&1 || missing+=(g++)
    command -v cmake >/dev/null 2>&1 || missing+=(cmake)
    command -v ninja >/dev/null 2>&1 || missing+=(ninja)
    pkg-config --exists Qt6Widgets Qt6Network 2>/dev/null || missing+=(Qt6)
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
            sudo apt-get install -y build-essential cmake ninja-build qt6-base-dev
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

build() {
    check_deps
    if [ "$FORCE_REBUILD" = 1 ] || [ ! -f build/CMakeCache.txt ] || [ ! -x build/aplus ]; then
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
