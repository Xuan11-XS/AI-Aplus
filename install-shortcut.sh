#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS_DIR="$DATA_HOME/applications"
ICONS_DIR="$DATA_HOME/icons/hicolor/512x512/apps"

mkdir -p "$APPS_DIR" "$ICONS_DIR"
install -m 644 "$ROOT/assets/aplus.png" "$ICONS_DIR/aplus.png"
sed "s|@ROOT@|$ROOT|g" "$ROOT/packaging/aplus.desktop.in" > "$APPS_DIR/aplus.desktop"
chmod 644 "$APPS_DIR/aplus.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS_DIR" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$DATA_HOME/icons/hicolor" >/dev/null 2>&1 || true
fi

DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
if [ -z "$DESKTOP_DIR" ] || [ "$DESKTOP_DIR" = "$HOME" ]; then
    DESKTOP_DIR="$HOME/Desktop"
fi
if [ -d "$DESKTOP_DIR" ] && [ -w "$DESKTOP_DIR" ]; then
    cp "$APPS_DIR/aplus.desktop" "$DESKTOP_DIR/Aplus.desktop"
    chmod +x "$DESKTOP_DIR/Aplus.desktop"
    gio set "$DESKTOP_DIR/Aplus.desktop" metadata::trusted true >/dev/null 2>&1 || true
    echo "桌面快捷方式：$DESKTOP_DIR/Aplus.desktop"
fi

echo "应用菜单快捷方式：$APPS_DIR/aplus.desktop"
echo "打开 GNOME 活动，搜索 Aplus 即可启动。"
