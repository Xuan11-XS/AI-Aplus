# Ubuntu 上编译运行 Aplus

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/aplus
```

- Qt 版本要求 >= 6.2：Ubuntu 22.04 带 Qt 6.2，24.04 带 Qt 6.4，都能编。
- 独立控制台按 `kgx` → `gnome-terminal` → `konsole` → `xfce4-terminal` 的顺序找终端；
  Ubuntu 默认 GNOME 会落到 gnome-terminal。包装脚本需要 `setsid`（util-linux）和
  `timeout`（coreutils），系统自带。
- 题目面板的网络：洛谷直连即可；Codeforces 被墙时，启动前
  `export http_proxy=https_proxy=http://127.0.0.1:7890`（换成你的代理），
  应用启动时会读这两个环境变量。
- `g++` 不在 PATH 里就设 `GXX`。
- 快捷方式：`./install-shortcut.sh`，和 Arch 一样装到应用菜单和桌面。
- 测试：`ctest --test-dir build --output-on-failure`。
- **不要**把别的机器编好的 `build/` 拷过来（比如 Arch 的 Qt 6.12 二进制在 Ubuntu 的 Qt 6.10 上会报
  `version 'Qt_6.12' not found`）。换机器后先 `./aplus.sh --rebuild`，它会清掉 build/ 重新编；
  脚本也会检测 CMakeCache 里的源码路径和当前目录不一致的情况，自动清理。
