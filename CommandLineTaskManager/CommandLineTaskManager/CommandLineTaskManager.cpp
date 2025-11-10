// CommandLineTaskManager.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
// 
// 
// 
// 
// 
// windows命令行任务管理器，核心功能包括：
// ---实时监控系统进程（PID、名称、内存占用），每3秒刷新一次；
// ---支持通过PID手动终止指定进程；
// ---通过多线程实现界面刷新与用户输入分离、避免卡顿
//
//
//
//
#define _WIN32_WINNT 0x0601  // 支持Windows 7及以上（推荐，兼容性最佳）
#define WIN32_LEAN_AND_MEAN  // 减少Windows.h头文件冗余内容，加快编译
#include <iostream>          //标准输入输出（cin/cout)
#include <vector>            //动态数组（存储进程列表）
#include <map>               //键值对容器（快速查询进程PID）
#include<mutex>              //互斥锁（解决多线程控制台冲突）
#include <string>
#include <thread>            //多线程支持
#include <chrono>            //时间函数（线程休眠）
#include<locale>             //字符编码转换（宽字符转string）
#include<codecvt>            //宽字符转UTF-8工具
#include <windows.h>         //Windows系统API（进程/内存操作）
#include <tlhelp32.h>        // 进程快照API头文件
#include <iomanip>           // 格式化输出（如setw）
#include <psapi.h>           // 必须包含，提供进程内存API
#pragma comment(lib, "psapi.lib")  // Windows系统库，链接psapi库（静态链接，避免运行时依赖）

using namespace std;

mutex mtx;          // 全局互斥锁，控制控制台输入输出同步（避免多线程冲突）


struct ProcessInfo {
    DWORD pid;           // 进程ID
    wstring name;        // 进程名称，宽字符，支持中文
    SIZE_T memoryUsage;  // 内存占用（字节）
};

// 宽字符串转普通字符串（解决中文乱码）
//Windows API返回的进程名称（szExeFile）是宽字符（wstring），直接用 cout 输出会乱码，需转为普通 string
string wstringToString(const wstring& wstr) {
    using convert_type = codecvt_utf8<wchar_t>;
    wstring_convert<convert_type, wchar_t> converter;
    return converter.to_bytes(wstr); // 将wstring转为UTF-8编码的string
}

// 枚举所有进程，返回进程信息列表（vector）：enumProcesses（）
//核心逻辑：通过Windows API CreateToolhelp32Snapshot 拍摄系统进程快照，
//          再用 Process32First/Next 遍历所有进程，获取PID和名称；
//内存获取：通过 OpenProcess 打开进程句柄，
//          调用 GetProcessMemoryInfo 获取私有内存占用（PrivateUsage）。
vector<ProcessInfo> enumProcesses() {
    //创建所有进程信息列表
    vector<ProcessInfo> processes;   

    //创建系统进程快照（获取所有进程的快照句柄）
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); 
    //如果快照创建失败：
    if (hSnapshot == INVALID_HANDLE_VALUE) {   
        cerr << "创建快照失败！错误码：" << GetLastError() << endl;
        return processes;
    }
    //那么创建成功后：
    PROCESSENTRY32 pe32;   //存储单个进程信息的结构体
    pe32.dwSize = sizeof(PROCESSENTRY32); // 必须初始化大小，否则API调用失败
    if (Process32First(hSnapshot, &pe32)) { // 获取第一个进程
        do {
            ProcessInfo pi;
            pi.pid = pe32.th32ProcessID; //进程ID
            pi.name = pe32.szExeFile; // 进程名称（如"notepad.exe"）

            // 获取内存占用（需打开进程获取句柄，需查询信息权限）
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pi.pid);
            if (hProcess != NULL) {    //进程打开成功
                PROCESS_MEMORY_COUNTERS_EX pmc;  //存储内存信息的结构体
                pmc.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);    //初始化大小
                //获取进程内存信息（PrivateUsage：私有内存，不共享）
                if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, pmc.cb)) {
                    pi.memoryUsage = pmc.PrivateUsage; // 私有内存（字节）
                }
                CloseHandle(hProcess); // 关闭进程句柄，释放资源，避免资源泄漏
            }

            processes.push_back(pi);  //将进程添加到列表
        } while (Process32Next(hSnapshot, &pe32)); // 遍历后续进程
    }
    CloseHandle(hSnapshot); // 关闭快照句柄

    return processes;
}

//终止进程函数：terminateProcess
//作用：通过PID终止进程，需获取 PROCESS_TERMINATE 权限，返回终止是否成功
bool terminateProcess(DWORD pid) {
    //打开进程，请求终止权限“PROCESS_TERMINATE”
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid); //获取权限
    //打开失败，可能权限不足或者PID不存在
    if (hProcess == NULL) {    
        cerr << "无法打开进程！错误码：" << GetLastError() << endl;
        return false;
    }
    //打开成功：
    bool success = TerminateProcess(hProcess, 0); // 终止进程，退出码0
    CloseHandle(hProcess);     //关闭句柄

    return success;
}

bool isRunning = true; // 控制线程循环的标志(主线程退出时设为false）

//多线程刷新函数 refreshProcesses
//多线程逻辑：独立线程循环执行，每3秒调用 enumProcesses 获取进程列表，清屏后输出；
//互斥锁：mtx.lock() 和 mtx.unlock() 确保刷新界面时，
//        用户输入线程被阻塞，避免控制台输出错乱；
//格式化输出：setw(n) 控制字段宽度，
//            fixed + setprecision(2) 确保内存显示为2位小数的MB值。
void refreshProcesses() {
    while (isRunning) {  //循环刷新，直到用户退出（输入0），isRunning=false
        mtx.lock();     //加锁：独占控制台，避免与用户输入冲突
       // system("cls"); // 清屏（Windows命令，Linux需替换为"clear"）
        cout << "===== 命令行任务管理器 =====" << endl;
        cout << "PID\t进程名称\t\t内存占用(MB)" << endl;
        cout << "-----------------------------------------" << endl;

        vector<ProcessInfo> processes = enumProcesses();  //获取所有进程
        // 使用map构建PID到进程信息的映射（快速查询）
        map<DWORD, ProcessInfo> pidToProcess;
        for (const auto& pi : processes) {
            pidToProcess[pi.pid] = pi;
            // 格式化输出：PID（6位对齐）、名称（20位对齐）、内存（转换为MB，保留2位小数）
            cout << setw(6) << pi.pid
                << setw(20) << wstringToString(pi.name)
                << setw(10) << fixed << setprecision(2)
                << (pi.memoryUsage / 1024.0 / 1024.0) << endl;   // 字节转MB（1MB=1024*1024字节）
        }

        cout << "\n操作提示：输入PID终止进程（输入0退出）：";
        mtx.unlock();    //// 解锁：允许主线程读取用户输入
        this_thread::sleep_for(chrono::seconds(3)); // 3秒刷新一次
    }
}
//多线程启动：创建 refreshThread 线程负责刷新界面，detach() 使其独立于主线程运行；
//用户交互：主线程通过 cin 读取PID，
//          输入 0 时设置 isRunning = false 终止刷新线程，然后退出程序；
//终止进程：调用 terminateProcess 终止用户指定PID的进程，并反馈结果。

int main() {
    // 创建刷新线程（执行refreshProcesses函数）
    thread refreshThread(refreshProcesses);
    refreshThread.detach(); // 分离线程（独立运行,主线程退出时自动结束）

    // 主线程处理用户输入
    DWORD targetPid;
    while (true) {   //循环读取用户输入
        mtx.lock();   //加锁：确保输入时不被刷新线程清屏
        cin >> targetPid;
        mtx.unlock();   //解锁：允许线程继续刷新
        // 输入0：退出程序
        if (targetPid == 0) { 
            isRunning = false;
            this_thread::sleep_for(chrono::seconds(1)); // 等待线程结束
            break;
        }
        // 输入PID：终止指定PID的进程
        if (terminateProcess(targetPid)) {
            cout << "进程 " << targetPid << " 已终止！" << endl;
        }
        else {
            cout << "终止进程 " << targetPid << " 失败！" << endl;
        }
    }
    return 0;
}
