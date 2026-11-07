# AGENTS.md —— Aplus 项目交接与协作协议

> 给接手本项目的任何 AI 代理（“同事”）看。代码作者署名 DeepSeek AI，这是用户明确要求的，不要删。

## 0. 开工前（必须做）

1. 读本文件。
2. 查记忆库：engram_search，scope=project，关键词 Aplus / GitHub / 代理 / gh。房间按需进 fact / decision / episode / skill。
3. `git log --oneline -10` 和 `git status` 看最近改动。
4. 不要靠聊天记忆猜状态——聊天记录只在同会话内可信，跨会话一律以 AGENTS.md + engram + 仓库为准。

## 1. 项目是什么

- Aplus：面向算法竞赛的单文件 C++ 编辑器。C++17 + Qt6 Widgets，没有 JS。
- 布局仿 Dev-C++：菜单栏 / 工具栏 / 编辑区 / 底部编译信息与运行输出 / 状态栏，全部 Qt 原生控件。
- 用户要求：写作不要 AI 味；UI 不自己发明样式；调试器先不做。

## 2. 构建与测试（Arch）

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/aplus
ctest --test-dir build --output-on-failure
```

- 无头截图验证 UI：`QT_QPA_PLATFORM=offscreen ./build/aplus --screenshot <路径>`
- 编译器：调 PATH 里的 g++，可用 `GXX` 环境变量覆盖；用户代码编译产物放在源文件旁（Windows 下 `.exe`）。

Windows 上的构建命令、g++ 的 PATH 要求、控制台怎么跑的，见 `docs/WINDOWS.md`。

## 3. 环境事实（本机）

### Arch（原来的机器）

- gh：`/home/XuanShao11/dsh/.gh-tools`（不在 PATH）
- gh 凭据与配置：`HOME=/home/XuanShao11/dsh/.ghhome`
- 外网走用户代理（Clash，全局模式）：`http_proxy=https_proxy=http://127.0.0.1:7897`
- GitHub：`Xuan11-XS/AI-Aplus`（公开），main 分支，本地已绑定 origin

GitHub 操作命令模板：

```bash
export HOME=/home/XuanShao11/dsh/.ghhome
export PATH=/home/XuanShao11/dsh/.gh-tools:$PATH
export https_proxy=http://127.0.0.1:7897 http_proxy=http://127.0.0.1:7897
```

### Windows（2026-11-08 补的机器）

- 仓库：`C:\Aplus`
- git：`C:\dsh-tools\git\cmd\git.exe`（MinGit 解压，不在系统 PATH）
  git 要设 `http.schannelCheckRevoke=false`，否则这台机器上拉不动 GitHub
- Qt 6.11.2 官方安装器：`C:\Qt\6.11.2\mingw_64`
- 工具链：`C:\Qt\Tools\mingw1310_64\bin`（g++ 13.1）、`C:\Qt\Tools\Ninja`、`C:\Qt\Tools\CMake_64`
- 没有 winget / scoop / choco / MSYS2，装东西直接下压缩包
- GitHub 凭据在 Windows 凭据管理器里（`Xuan11-XS`），gh 没装
- 会话里的 DSH 沙箱把子进程的写操作限制在工作区，`%TEMP%` 不可写；
  跑 `ctest` 或编译前把 `TEMP`/`TMP` 指到 `C:\Aplus\build\tmp`，否则 QTemporaryDir 直接失败

## 4. 状态（最后更新 2026-11-08）

- ✅ 已做：编辑器、编译运行、时间限制、编译错误双击跳转、独立控制台（kgx/GNOME Console）、Ctrl+滚轮缩放、应用图标、GNOME 快捷方式；源码已推公开仓库，简介与 README 开头署 DeepSeek AI。
- ✅ 已做（2026-11-08，Windows）：用 Qt 官方 MinGW 套件编出 `build/aplus.exe`，`ctest` 全过；独立控制台补上 Windows 版——PowerShell 开新控制台窗口跑程序，交互输入可用，Ctrl+C 只打断程序，Ctrl+F2 按 pid 文件结束程序，窗口留着打收尾信息。冒烟验证过编译运行、退出码、超时、停止、控制台关窗。
- 💡 计划（用户感兴趣）：内联洛谷/CF。顺序：题面+样例一键填入 → 本地对拍 → CF 官方接口提交；洛谷只做“题面+样例”，不碰登录提交（有风控风险）。
- 🕳 已修坑（别重踩）：
  - Linux：控制台交互输入失效（bash 给裸后台任务接 /dev/null，须显式 `<&3` 从终端读）；Ctrl+C 被继承忽略（用 `trap ':' INT` 而不是 `trap '' INT`）。
  - Windows：`run.ps1` 必须带 UTF-8 BOM 写，PowerShell 5.1 没 BOM 就按 ANSI 读，中文注释会把换行吞掉、脚本直接语法错；启动要带 `-ExecutionPolicy Bypass`；壳进程用 `SetConsoleCtrlHandler(NULL, true)` 忽略 Ctrl+C；PowerShell 5.1 的 `ProcessStartInfo` 没有 `ArgumentList`。

## 5. 交接协议（同事之间怎么说话）

每个会话收工时做三件事：

1. 关键事实/决策写进 engram（scope=project；变更用 engram_update 建取代链，不要反复保存互相矛盾的事实）。
2. 把上面“状态”段更新到最新，commit 消息写清做了什么。
3. 与用户相关的长期约定进 engram（scope=user）。

冲突裁决顺序：**AGENTS.md 的状态段 > engram 最新取代链 > 聊天记忆**。

## 6. 提交规范

- 仓库名、简介、README 开头的 DeepSeek AI 署名不许删。
- commit 用中文，说清“做了什么”，不说“优化/改进”这种空话。
- 不提交 build/ 和编译产物（.gitignore 已配好）。
- 凭据、代理端口这类东西只放在 AGENTS.md 的“环境事实”，不写进代码。
