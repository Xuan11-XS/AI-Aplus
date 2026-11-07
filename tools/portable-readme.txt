Aplus 便携版（Windows x64，Qt 6.11.2 + g++ 13.1）

双击 Aplus.cmd 启动。它先把自带的 g++ 加进 PATH，再打开 Aplus。

文件夹里有什么
  aplus.exe                主程序
  Qt6*.dll、platforms/ 等   Qt 运行库，由 windeployqt 生成；platforms 删了窗口打不开
  toolchain/mingw64/        精简过的 MinGW g++，编译你写的 C++ 用它
  workspace/                默认工作区，放 A+B 模板 main.cpp
  Aplus.cmd                 启动脚本

用法
  F9 编译，F10 运行，F11 编译并运行，Ctrl+F2 停止。
  菜单“运行 → 在独立控制台中运行”默认勾着，会弹一个控制台窗口跑程序；
  取消勾选就退回底部内嵌输出（这种模式才套时间限制）。

注意
  别直接双击 aplus.exe：那样 PATH 里没有 g++，编译会提示找不到编译器。
  想拷到别的机器，整个 Aplus 文件夹一起拷走即可，目标机器不用装 Qt 或 MinGW。

重新打包
  仓库根目录：
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64
    cmake --build build
    powershell -ExecutionPolicy Bypass -File tools/package-windows.ps1
  产物在 package/Aplus，脚本会整个重建这个目录。
