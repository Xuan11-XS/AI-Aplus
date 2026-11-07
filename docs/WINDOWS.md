# Windows 上编译 Aplus

Aplus 的代码是按跨平台写的：Qt 6 Widgets + CMake + g++，Linux 和 Windows 共用同一份源码。
在 Windows 上重新编译一遍就能得到 `aplus.exe`，不需要改代码。

Arch 上不能直接编出 exe（缺 mingw-w64 + Windows 版 Qt 的交叉工具链），所以这一步要在
Windows 机器上做。

## 方式一：MSYS2（推荐）

1. 从 <https://www.msys2.org/> 装 MSYS2。
2. 打开 **MSYS2 UCRT64** 终端，先更新再装工具链：

   ```bash
   pacman -Syu
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc \
       mingw-w64-ucrt-x86_64-cmake \
       mingw-w64-ucrt-x86_64-ninja \
       mingw-w64-ucrt-x86_64-qt6-base \
       mingw-w64-ucrt-x86_64-qt6-tools
   ```

   `qt6-tools` 里带 `windeployqt`，用来打包 Qt 运行库。

3. 进入项目目录，编译：

   ```bash
   cd aplus
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

   UCRT64 终端里 CMake 会自动找到 Qt；找不到就显式指定：

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
   ```

4. 产物是 `build/aplus.exe`。直接运行即可；要拷到别的机器，先把 Qt DLL 凑齐：

   ```bash
   windeployqt build/aplus.exe
   ```

   之后把整个 `build` 目录（或 windeployqt 生成的那堆 DLL + exe）一起拷走。

## 方式二：Qt 官方在线安装器

装 Qt 6.x 的 **MinGW 64-bit** 套件（带 CMake/Ninja 或 Qt Creator），然后：

```bat
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.x.y/mingw_64
cmake --build build
C:/Qt/6.x.y/mingw_64/bin/windeployqt.exe build/aplus.exe
```

MinGW 套件自带 `g++`，Aplus 编译你写的代码用的就是它。

这条路实际走通过：Qt 6.11.2（`C:/Qt/6.11.2/mingw_64`）+ 自带 MinGW 13.1 + 自带 Ninja/CMake，
用下面的命令能直接编出 `build/aplus.exe`，`ctest` 也过：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64 ^
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe
cmake --build build
ctest --test-dir build --output-on-failure
```

注意 MinGW 的 `bin` 要进 PATH：`g++.exe` 本身能找到还不够，它拉起来的 `cc1plus.exe`
要靠 PATH 找 `libgmp` 这些 DLL，缺了会静默失败。

## 让 Aplus 找到 g++

Aplus 本身不需要装编译器，它调用 PATH 里的 `g++`：

- 在 UCRT64 终端里启动 Aplus，g++ 自动可用；
- 从资源管理器双击启动时，把 `C:\msys64\ucrt64\bin` 加进系统 PATH，
  或者设环境变量 `GXX=C:\msys64\ucrt64\bin\g++.exe` 再启动 Aplus。

## 快捷方式

右键 `aplus.exe` → 发送到 → 桌面快捷方式。然后把快捷方式的属性里
“起始位置(Start in)”改成项目根目录，`workspace/` 就在它下面。

## 独立控制台

运行菜单里的“在独立控制台中运行”在 Windows 上是有的：Aplus 用 PowerShell 开一个
新控制台窗口跑你的程序，和 Dev-C++ 的控制台一个用法。

- 程序在控制台窗口里可以直接读键盘，交互输入不用先填“程序输入”；
- 窗口里会打出退出码和耗时，停在“按任意键关闭窗口...”；
- Ctrl+F2 停止：Aplus 按 pid 结束程序，窗口留着打完收尾信息；
- Ctrl+C 只打断你的程序，不会把壳进程一起带走。

前提是系统里有 Windows PowerShell（Windows 10/11 自带，
`%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`）。找不到时会退回底部内嵌输出，
菜单项变灰，鼠标悬停能看到原因。

## 和 Linux 版的差别

- F9/F10/F11、时间限制、停止、编译错误跳转、`Ctrl+滚轮`、图标这些都一样。
- 独立控制台：Linux 调 GNOME Console / konsole 等终端模拟器，
  Windows 调 PowerShell 开 `powershell.exe` 控制台窗口，跑的是同一套“壳脚本 + status 文件”协议
  （`src/consolerunner.cpp`，Linux 写 `run.sh`，Windows 写 `run.ps1`）。
- 控制台模式目前和 Linux 一样不套时间限制；要测时限用内嵌输出（F10）。

## 注意

- 别混用 MSVC 和 MinGW：Qt 套件和 `g++` 都用 MinGW 的。
- 编译用户代码用 `LC_ALL=C` 这些环境变量在 Windows 上无效，直接忽略，不影响功能。
