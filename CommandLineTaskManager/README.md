一、项目名称：**Windows命令行任务管理器**



二、项目简介

一个轻量级的Windows命令行进程监控工具，基于C++多线程实现，支持实时显示进程信息和手动终止进程，界面无卡顿。



三、功能特点

📊 实时进程监控：每3秒自动刷新系统进程列表，显示PID、进程名称、内存占用（MB）。

🛑 进程终止：通过PID手动终止指定进程，支持快速操作。

🚀 多线程设计：监控线程与用户输入线程分离，避免界面卡顿和输出错乱。



四、编译与运行

1、环境要求

系统：Windows 7及以上（32/64位）

编译器：Visual Studio 2019+（或支持C++11的其他编译器）

依赖：Windows SDK（默认包含在VS安装中）

2、编译步骤

①创建项目：

打开Visual Studio → 新建“控制台应用”项目（名称建议为 CommandLineTaskManager）。

②添加代码：

将 CommandLineTaskManager.cpp 代码复制到项目源文件中（替换默认的 main.cpp）。

③配置项目：

确保代码中包含 #pragma comment(lib, "psapi.lib")（链接系统进程库，无需额外下载）。

项目属性 → 常规 → 平台工具集：选择对应Windows SDK版本（如“Windows 10 SDK”）。

④生成可执行文件：

按 Ctrl+Shift+B 编译，生成的 .exe 文件默认路径为 项目目录/x64/Debug/（或 Release 目录）。

3、运行方法

\# 直接双击编译生成的.exe文件，或通过命令行启动

cd path/to/executable

TaskManager.exe



五、使用指南

1、界面说明

启动后显示进程列表，格式为：

===== 命令行任务管理器 =====

PID    进程名称              内存占用(MB)

---

1234   notepad.exe           5.23

5678   chrome.exe            120.50

...

操作提示：输入PID终止进程（输入0退出）：

2、核心操作

终止进程：输入目标进程的PID（如 1234），按回车即可尝试终止。

退出程序：输入 0 并回车，程序将关闭所有线程并退出。

3、注意事项

终止系统关键进程（如 svchost.exe、csrss.exe）可能导致系统不稳定，需谨慎操作。

若提示“无法打开进程”，可能是权限不足（建议以管理员身份运行程序）或PID不存在。



六、代码结构

1、核心功能实现（函数/模块  +	作用）

①enumProcesses()：枚举系统所有进程，通过 CreateToolhelp32Snapshot 获取PID、名称，调用 GetProcessMemoryInfo 获取内存占用。

②terminateProcess()：通过 OpenProcess 请求 PROCESS\_TERMINATE 权限，调用 TerminateProcess 终止指定PID的进程。

③refreshProcesses()：独立线程函数，每3秒刷新进程列表，通过 mutex 互斥锁解决控制台输出冲突。

④wstringToString()：宽字符串转UTF-8，解决Windows API返回的进程名称中文乱码问题。

2、多线程设计

刷新线程：负责枚举进程和控制台输出，每3秒刷新一次。

主线程：处理用户输入，通过互斥锁（mutex）与刷新线程同步，避免界面错乱。



七、目录结构

CommandLineTaskManager/

├─ src/                # 源代码目录

│  └─ TaskManager.cpp  # 主程序代码（包含所有功能实现）

└─ README.md           # 项目说明文档



八、扩展建议

功能扩展：增加CPU占用率监控（通过 GetSystemTimes 实现）、进程筛选（按名称/内存排序）。

界面优化：使用Windows API替代 system("cls") 实现高效清屏，避免闪烁。

跨平台适配：替换Windows API为 procfs（Linux）或 sysctl（macOS），实现跨平台支持。



九、许可证

本项目采用MIT许可证 - 详情参见 LICENSE 文件（若未提供，默认保留所有权利）。



